// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
 * g_nav.c -- the re-release's navigation nodes (bots/navigation/<map>.nav)
 * and its path finder over them, for its monsters' pathing.
 *
 * The re-release's engine does this, closed; this is its code as id and
 * Nightdive gave it to q2pro-ng (src/server/nav.c, GPL), made the game's
 * own. A map with no nav file has no paths, and its monsters move as they
 * always have.
 */

#include "g_local.h"

#define NAV_MAGIC		(('3'<<24)+('V'<<16)+('A'<<8)+'N')
#define NAV_VERSION_2	2
#define NAV_VERSION_3	3
#define NAV_VERSION_4	4
#define NAV_VERSION_6	6

#define INVALID_ID		0xffff

// node flags
#define NodeFlag_Teleporter			0x0001
#define NodeFlag_Pusher				0x0002
#define NodeFlag_Elevator			0x0004
#define NodeFlag_Ladder				0x0008
#define NodeFlag_UnderWater			0x0010
#define NodeFlag_CheckForHazard		0x0020
#define NodeFlag_CheckHasFloor		0x0040
#define NodeFlag_CheckInSolid		0x0080
#define NodeFlag_NoMonsters			0x0100
#define NodeFlag_Crouch				0x0200
#define NodeFlag_NoPOI				0x0400
#define NodeFlag_CheckInLiquid		0x0800
#define NodeFlag_CheckDoorLinks		0x1000
#define NodeFlag_Disabled			0x2000

#define NodeFlag_ConditionalMask	(NodeFlag_CheckDoorLinks | NodeFlag_CheckForHazard | \
	NodeFlag_CheckHasFloor | NodeFlag_CheckInLiquid | NodeFlag_CheckInSolid)

// link types
enum {
	NavLinkType_Walk,
	NavLinkType_LongJump,
	NavLinkType_Teleport,
	NavLinkType_WalkOffLedge,
	NavLinkType_Pusher,
	NavLinkType_BarrierJump,
	NavLinkType_Elevator,
	NavLinkType_Train,
	NavLinkType_Manual_LongJump,
	NavLinkType_Crouch,
	NavLinkType_Ladder,
	NavLinkType_Manual_BarrierJump,
	NavLinkType_PivotAndJump,
	NavLinkType_RocketJump,
	NavLinkType_Unknown
};

// link flags
#define NavLinkFlag_TeamRed		0x01
#define NavLinkFlag_TeamBlue	0x02
#define NavLinkFlag_AllTeams	(NavLinkFlag_TeamRed | NavLinkFlag_TeamBlue)

typedef struct nav_link_s nav_link_t;
typedef struct nav_edict_s nav_edict_t;

typedef struct
{
	int			flags;
	int			num_links;
	nav_link_t	*links;
	int			id;
	float		radius;
	vec3_t		origin;
} nav_node_t;

typedef struct
{
	vec3_t		funnel;
	vec3_t		start;
	vec3_t		end;
	vec3_t		ladder_plane;
} nav_traversal_t;

struct nav_link_s
{
	nav_node_t		*target;
	int				type;
	int				flags;
	nav_traversal_t	*traversal;
	nav_edict_t		*edict;
};

struct nav_edict_s
{
	nav_link_t	*link;
	int			model;		// the mover's modelindex ("*N" is N+1)
	vec3_t		mins, maxs;
	edict_t		*game_edict;
};

// the open set, a binary heap on f
typedef struct
{
	float		f;
	int			node;
} nav_open_t;

static struct
{
	int				num_nodes;
	int				num_links;
	int				num_traversals;
	int				num_edicts;
	float			heuristic;

	nav_node_t		*nodes;
	nav_link_t		*links;
	nav_traversal_t	*traversals;
	nav_edict_t		*edicts;

	int				num_conditional_nodes;
	nav_node_t		**conditional_nodes;

	// the search's
	nav_open_t		*open;
	int				num_open, max_open;
	unsigned short	*came_from, *went_to;
	float			*g_score;

	qboolean		setup_entities;
	float			setup_time;
} nav;

static const float NavFloorDistance = 96.0f;

static cvar_t	*nav_enable;

/*
 * reading the file
 */
typedef struct
{
	byte	*data;
	int		len, pos;
	qboolean overflowed;
} navbuf_t;

static unsigned Nav_ReadLong (navbuf_t *b)
{
	byte	*p;

	if (b->pos + 4 > b->len)
	{
		b->overflowed = true;
		return 0;
	}
	p = b->data + b->pos;
	b->pos += 4;
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static int Nav_ReadShort (navbuf_t *b)
{
	byte	*p;

	if (b->pos + 2 > b->len)
	{
		b->overflowed = true;
		return INVALID_ID;
	}
	p = b->data + b->pos;
	b->pos += 2;
	return p[0] | (p[1] << 8);
}

static int Nav_ReadByte (navbuf_t *b)
{
	if (b->pos + 1 > b->len)
	{
		b->overflowed = true;
		return 0;
	}
	return b->data[b->pos++];
}

static float Nav_ReadFloat (navbuf_t *b)
{
	union { unsigned u; float f; } v;

	v.u = Nav_ReadLong (b);
	return v.f;
}

static void Nav_ReadVector (navbuf_t *b, vec3_t v)
{
	v[0] = Nav_ReadFloat (b);
	v[1] = Nav_ReadFloat (b);
	v[2] = Nav_ReadFloat (b);
}

static void *Nav_Alloc (int count, int size)
{
	return gi.TagMalloc (count ? count * size : 1, TAG_LEVEL);
}

/*
=============
Nav_Load

The level's nav file, if it has one. Its memory is the level's, freed
with it.
=============
*/
void Nav_Load (char *mapname)
{
	char		filename[MAX_QPATH];
	byte		*data;
	navbuf_t	b;
	unsigned	v;
	int			i, c, len, first_link, target, traversal, link;
	char		*err = NULL;

	memset (&nav, 0, sizeof(nav));

	if (!nav_enable)
		nav_enable = gi.cvar ("nav_enable", "1", 0);
	if (!nav_enable->value || !gi.LoadFile)
		return;

	Com_sprintf (filename, sizeof(filename), "bots/navigation/%s.nav", mapname);
	len = gi.LoadFile (filename, (void **)&data);
	if (!data)
		return;

	b.data = data;
	b.len = len;
	b.pos = 0;
	b.overflowed = false;

#define NAV_VERIFY(condition, error) \
	if (!(condition)) { err = error; goto fail; }

	NAV_VERIFY (len >= 7*4, "file too small");

	v = Nav_ReadLong (&b);
	NAV_VERIFY (v == NAV_MAGIC, "bad magic");

	v = Nav_ReadLong (&b);
	NAV_VERIFY (v >= NAV_VERSION_2 && v <= NAV_VERSION_6, "bad version");

	nav.num_nodes = Nav_ReadLong (&b);
	nav.num_links = Nav_ReadLong (&b);
	nav.num_traversals = Nav_ReadLong (&b);
	nav.heuristic = Nav_ReadFloat (&b);
	NAV_VERIFY (nav.num_nodes > 0, "no nodes");
	NAV_VERIFY (nav.num_links > 0, "no links");
	NAV_VERIFY (nav.num_nodes < INVALID_ID, "too many nodes");
	NAV_VERIFY (nav.num_links < INVALID_ID, "too many links");
	NAV_VERIFY (nav.num_traversals < INVALID_ID, "too many traversals");

	nav.nodes = Nav_Alloc (nav.num_nodes, sizeof(nav.nodes[0]));
	nav.links = Nav_Alloc (nav.num_links, sizeof(nav.links[0]));
	nav.traversals = Nav_Alloc (nav.num_traversals, sizeof(nav.traversals[0]));

	for (i = 0; i < nav.num_nodes; i++)
	{
		nav_node_t	*node = nav.nodes + i;

		node->id = i;
		node->flags = Nav_ReadShort (&b);
		node->num_links = Nav_ReadShort (&b);
		first_link = Nav_ReadShort (&b);
		NAV_VERIFY (!b.overflowed, "read past end of file");
		NAV_VERIFY (first_link + node->num_links <= nav.num_links, "bad node links");
		node->links = &nav.links[first_link];
		node->radius = Nav_ReadShort (&b);

		if (node->flags & NodeFlag_ConditionalMask)
			nav.num_conditional_nodes++;
	}

	nav.conditional_nodes = Nav_Alloc (nav.num_conditional_nodes, sizeof(nav.conditional_nodes[0]));

	for (i = 0, c = 0; i < nav.num_nodes; i++)
	{
		nav_node_t	*node = nav.nodes + i;

		Nav_ReadVector (&b, node->origin);

		if (node->flags & NodeFlag_ConditionalMask)
			nav.conditional_nodes[c++] = node;
	}

	for (i = 0; i < nav.num_links; i++)
	{
		nav_link_t	*l = nav.links + i;

		target = Nav_ReadShort (&b);
		NAV_VERIFY (target < nav.num_nodes, "bad link target");
		l->target = &nav.nodes[target];
		l->type = Nav_ReadByte (&b);
		l->flags = Nav_ReadByte (&b);

		if (v < NAV_VERSION_3)
			l->flags = NavLinkFlag_AllTeams;
		else if (v < NAV_VERSION_6)		// strip old green/yellow team flags
			l->flags &= ~(4|8);

		l->traversal = NULL;
		l->edict = NULL;
		traversal = Nav_ReadShort (&b);
		if (traversal != INVALID_ID)
		{
			NAV_VERIFY (traversal < nav.num_traversals, "bad link traversal");
			l->traversal = &nav.traversals[traversal];
		}
	}

	for (i = 0; i < nav.num_traversals; i++)
	{
		nav_traversal_t	*t = nav.traversals + i;

		Nav_ReadVector (&b, t->funnel);
		Nav_ReadVector (&b, t->start);
		Nav_ReadVector (&b, t->end);
		if (v >= NAV_VERSION_4)
			Nav_ReadVector (&b, t->ladder_plane);
	}

	nav.num_edicts = Nav_ReadLong (&b);
	NAV_VERIFY (!b.overflowed, "read past end of file");
	NAV_VERIFY (nav.num_edicts <= MAX_EDICTS, "too many edicts");

	nav.edicts = Nav_Alloc (nav.num_edicts, sizeof(nav.edicts[0]));

	for (i = 0; i < nav.num_edicts; i++)
	{
		nav_edict_t	*e = nav.edicts + i;

		link = Nav_ReadShort (&b);
		NAV_VERIFY (link < nav.num_links, "bad edict link");
		e->link = &nav.links[link];
		e->link->edict = e;
		e->game_edict = NULL;
		e->model = Nav_ReadLong (&b);
		Nav_ReadVector (&b, e->mins);
		Nav_ReadVector (&b, e->maxs);
	}

	NAV_VERIFY (!b.overflowed, "read past end of file");

	gi.dprintf ("Loaded %s (version %d): %d nodes, %d links, %d traversals, %d edicts\n",
		filename, v, nav.num_nodes, nav.num_links, nav.num_traversals, nav.num_edicts);

	gi.FreeFile (data);

	nav.g_score = Nav_Alloc (nav.num_nodes, sizeof(nav.g_score[0]));
	nav.came_from = Nav_Alloc (nav.num_nodes, sizeof(nav.came_from[0]));
	nav.went_to = Nav_Alloc (nav.num_nodes, sizeof(nav.went_to[0]));
	nav.max_open = nav.num_nodes + nav.num_links;
	nav.open = Nav_Alloc (nav.max_open, sizeof(nav.open[0]));
	nav.setup_time = level.time + 1;
	return;

fail:
	gi.FreeFile (data);
	gi.dprintf ("Couldn't load %s: %s\n", filename, err);
	memset (&nav, 0, sizeof(nav));		// what was allocated goes with the level
}

qboolean Nav_Available (void)
{
	return nav.nodes != NULL;
}

/*
 * the search
 */
typedef struct
{
	nav_request_t	*request;
	nav_info_t		*info;
	nav_node_t		*start, *goal;
} nav_search_t;

static float Nav_DistanceSquared (vec3_t a, vec3_t b)
{
	vec3_t	d;

	VectorSubtract (a, b, d);
	return DotProduct (d, d);
}

static float Nav_Length2D (vec3_t d)
{
	return sqrt (d[0]*d[0] + d[1]*d[1]);
}

static float Nav_Heuristic (nav_search_t *s, nav_node_t *node)
{
	return Nav_DistanceSquared (s->goal->origin, node->origin);
}

static float Nav_Weight (nav_node_t *node, nav_link_t *link)
{
	if (link->type == NavLinkType_Teleport)
		return 1.0f;

	return Nav_DistanceSquared (node->origin, link->target->origin);
}

static qboolean Nav_NodeAccessible (nav_search_t *s, nav_node_t *node)
{
	if (node->flags & NodeFlag_Disabled)
		return false;

	if (s->request->ignoreNodeFlags)
		return !(node->flags & NodeFlag_NoPOI);

	if (node->flags & (NodeFlag_NoMonsters | NodeFlag_Crouch | NodeFlag_Ladder | NodeFlag_Elevator | NodeFlag_Pusher | NodeFlag_Teleporter))
		return false;

	if ((node->flags & NodeFlag_UnderWater) && !(s->request->pathFlags & PathFlags_Water))
		return false;

	if (!(node->flags & NodeFlag_UnderWater) && !(s->request->pathFlags & PathFlags_Walk))
		return false;

	return true;
}

static qboolean Nav_LinkAccessible (nav_search_t *s, nav_node_t *node, nav_link_t *link)
{
	nav_request_t	*req = s->request;
	nav_traversal_t	*trv = link->traversal;
	edict_t			*e;

	if (!Nav_NodeAccessible (s, link->target))
		return false;

	if (req->ignoreNodeFlags)
		return true;

	// a link across a mover that isn't a door (a plat, a train) is not
	// walked
	if (link->edict)
	{
		e = link->edict->game_edict;
		if (e && e->inuse && e->s.modelindex == link->edict->model && !(e->flags & FL_DOOR))
			return false;
	}

	switch (link->type)
	{
	case NavLinkType_Walk:
		return true;

	case NavLinkType_LongJump:
		return (req->pathFlags & PathFlags_LongJump) != 0;

	case NavLinkType_WalkOffLedge:
		if (!(req->pathFlags & PathFlags_WalkOffLedge))
			return false;
		if (trv && trv->start[2] - trv->end[2] > req->dropHeight)
			return false;
		return true;

	case NavLinkType_BarrierJump:
		if (!(req->pathFlags & PathFlags_BarrierJump))
			return false;
		if (trv && trv->end[2] - trv->start[2] > req->jumpHeight)
			return false;
		return true;

	default:
		return false;
	}
}

static nav_node_t *Nav_ClosestNodeTo (nav_search_t *s, vec3_t p)
{
	nav_request_t	*req = s->request;
	float			w = 1e30f, l, min_z, max_z;
	nav_node_t		*c = NULL, *node;
	vec3_t			d, end;
	trace_t			tr;
	int				i;

	min_z = p[2] - req->minHeight;
	max_z = p[2] + req->maxHeight;

	for (i = 0; i < nav.num_nodes; i++)
	{
		node = &nav.nodes[i];
		if (!Nav_NodeAccessible (s, node))
			continue;

		if (node->origin[2] < min_z || node->origin[2] > max_z)
			continue;

		VectorSubtract (node->origin, p, d);
		l = Nav_Length2D (d);
		if (l > req->radius)
			continue;
		if (l >= w)
			continue;

		VectorCopy (node->origin, end);
		end[2] += 32;
		tr = gi.trace (p, vec3_origin, vec3_origin, end, NULL, MASK_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP);
		if (tr.fraction < 1.0f)
			continue;

		w = l;
		c = node;
	}

	return c;
}

static nav_link_t *Nav_GetLink (nav_node_t *a, nav_node_t *b)
{
	nav_link_t	*link;

	for (link = a->links; link != a->links + a->num_links; link++)
		if (link->target == b)
			return link;

	return NULL;
}

static qboolean Nav_NodeReached (vec3_t pos, nav_node_t *node)
{
	vec3_t	d;

	VectorSubtract (node->origin, pos, d);
	return Nav_Length2D (d) <= node->radius && fabs (d[2]) <= 64;
}

static void Nav_PushOpen (int node, float f)
{
	int			i, parent;
	nav_open_t	t;

	if (nav.num_open >= nav.max_open)
		return;

	i = nav.num_open++;
	nav.open[i].node = node;
	nav.open[i].f = f;

	while (i > 0)
	{
		parent = (i - 1) / 2;
		if (nav.open[parent].f <= nav.open[i].f)
			break;
		t = nav.open[parent];
		nav.open[parent] = nav.open[i];
		nav.open[i] = t;
		i = parent;
	}
}

static int Nav_PopOpen (void)
{
	int			node, i, l, r, m;
	nav_open_t	t;

	node = nav.open[0].node;
	nav.open[0] = nav.open[--nav.num_open];

	i = 0;
	while (1)
	{
		l = i * 2 + 1;
		r = l + 1;
		m = i;
		if (l < nav.num_open && nav.open[l].f < nav.open[m].f)
			m = l;
		if (r < nav.num_open && nav.open[r].f < nav.open[m].f)
			m = r;
		if (m == i)
			break;
		t = nav.open[m];
		nav.open[m] = nav.open[i];
		nav.open[i] = t;
		i = m;
	}

	return node;
}

#define PATH_POINT_TOO_CLOSE	(64 * 64)

static void Nav_ReachedGoal (nav_search_t *s, int current)
{
	nav_request_t	*request = s->request;
	nav_info_t		*info = s->info;
	int				num_points = 0, n, p, first_point;
	nav_link_t		*link = NULL;
	float			dist;

	// reverse the order of came_from into went_to
	n = current;
	while (nav.came_from[n] != INVALID_ID)
	{
		num_points++;
		n = nav.came_from[n];
	}

	n = current;
	p = 0;
	while (nav.came_from[n] != INVALID_ID)
	{
		n = nav.went_to[num_points - p - 1] = nav.came_from[n];
		p++;
	}

	// num_points now holds the points between start and current, at
	// least 1, since start can't be the same as the goal
	first_point = 0;

	if (num_points > 1)
	{
		link = Nav_GetLink (&nav.nodes[nav.went_to[0]], &nav.nodes[nav.went_to[1]]);

		// if the link isn't a traversal, skip the first node if we're
		// past it or touching it
		if (link && !request->ignoreNodeFlags && !link->traversal
			&& Nav_NodeReached (request->start, &nav.nodes[nav.went_to[0]]))
			first_point++;
	}

	// store the path, for whoever asked for it
	if (request->maxPoints)
	{
		// if we're too far from the first node, add in our current position
		dist = Nav_DistanceSquared (request->start, nav.nodes[nav.went_to[first_point]].origin);
		if (dist > PATH_POINT_TOO_CLOSE)
		{
			if (info->numPathPoints < request->maxPoints)
				VectorCopy (request->start, request->points[info->numPathPoints]);
			info->numPathPoints++;
		}

		for (p = first_point; p < num_points; p++)
		{
			if (info->numPathPoints < request->maxPoints)
				VectorCopy (nav.nodes[nav.went_to[p]].origin, request->points[info->numPathPoints]);
			info->numPathPoints++;
		}

		// the end point, if we have room
		dist = Nav_DistanceSquared (request->goal, nav.nodes[nav.went_to[num_points - 1]].origin);
		if (dist > PATH_POINT_TOO_CLOSE)
		{
			if (info->numPathPoints < request->maxPoints)
				VectorCopy (request->goal, request->points[info->numPathPoints]);
			info->numPathPoints++;
		}
	}

	info->pathDistSqr = nav.g_score[current];

	if (request->ignoreNodeFlags)
	{
		info->returnCode = PathReturnCode_RawPathFound;
		return;
	}

	// where to move first
	if (link && link->traversal)
	{
		VectorCopy (link->traversal->start, info->firstMovePoint);
		VectorCopy (link->traversal->end, info->secondMovePoint);
		info->returnCode = PathReturnCode_TraversalPending;
	}
	else
	{
		VectorCopy (nav.nodes[nav.went_to[first_point]].origin, info->firstMovePoint);
		if (first_point + 1 < num_points)
			VectorCopy (nav.nodes[nav.went_to[first_point + 1]].origin, info->secondMovePoint);
		else
			VectorCopy (request->goal, info->secondMovePoint);
		info->returnCode = PathReturnCode_InProgress;
	}
}

/*
=============
Nav_GetPathToGoal

The path from request->start to request->goal over the nodes: where to
move first (and next) in info. False for an error (info->returnCode).
=============
*/
qboolean Nav_GetPathToGoal (nav_request_t *request, nav_info_t *info)
{
	nav_search_t	s;
	nav_node_t		*current_node;
	nav_link_t		*link;
	vec3_t			d;
	int				i, current, target_id, start_id, goal_id;
	float			temp_g_score;

	memset (info, 0, sizeof(*info));
	s.request = request;
	s.info = info;

	if (!nav.nodes)
	{
		info->returnCode = PathReturnCode_NoNavAvailable;
		return false;
	}

	if (!(request->pathFlags & (PathFlags_Walk | PathFlags_Water)))
	{
		info->returnCode = PathReturnCode_MissingWalkOrSwimFlag;
		return false;
	}

	s.start = Nav_ClosestNodeTo (&s, request->start);
	if (!s.start)
	{
		info->returnCode = PathReturnCode_NoStartNode;
		return false;
	}

	s.goal = Nav_ClosestNodeTo (&s, request->goal);
	if (!s.goal)
	{
		info->returnCode = PathReturnCode_NoGoalNode;
		return false;
	}

	VectorSubtract (s.goal->origin, request->start, d);
	if (s.start == s.goal || VectorLength (d) <= request->moveDist)
	{
		info->returnCode = PathReturnCode_ReachedGoal;
		VectorCopy (request->goal, info->firstMovePoint);
		VectorCopy (request->goal, info->secondMovePoint);
		return true;
	}

	if (!request->ignoreNodeFlags)
	{
		if (gi.pointcontents (request->start) & MASK_SOLID)
		{
			info->returnCode = PathReturnCode_InvalidStart;
			return false;
		}
		if (gi.pointcontents (request->goal) & MASK_SOLID)
		{
			info->returnCode = PathReturnCode_InvalidGoal;
			return false;
		}
	}

	start_id = s.start->id;
	goal_id = s.goal->id;

	for (i = 0; i < nav.num_nodes; i++)
		nav.g_score[i] = 1e30f;

	nav.num_open = 0;
	nav.came_from[start_id] = INVALID_ID;
	nav.g_score[start_id] = 0;
	Nav_PushOpen (start_id, Nav_Heuristic (&s, s.start));

	while (nav.num_open)
	{
		current = Nav_PopOpen ();

		if (current == goal_id)
		{
			Nav_ReachedGoal (&s, current);
			return true;
		}

		current_node = &nav.nodes[current];

		for (i = 0; i < current_node->num_links; i++)
		{
			link = &current_node->links[i];

			if (!Nav_LinkAccessible (&s, current_node, link))
				continue;

			target_id = link->target->id;
			temp_g_score = nav.g_score[current] + Nav_Weight (current_node, link);

			if (temp_g_score >= nav.g_score[target_id])
				continue;

			nav.came_from[target_id] = current;
			nav.g_score[target_id] = temp_g_score;

			Nav_PushOpen (target_id, temp_g_score + Nav_Heuristic (&s, link->target));
		}
	}

	info->returnCode = PathReturnCode_NoPathFound;
	return false;
}

/*
 * the nodes that are open or shut by what is there
 */
static void Nav_GetNodeBounds (nav_node_t *node, vec3_t mins, vec3_t maxs)
{
	VectorSet (mins, -16, -16, -24);
	VectorSet (maxs, 16, 16, 32);

	if (node->flags & NodeFlag_Crouch)
		maxs[2] = 4.0f;
}

static void Nav_GetNodeTraceOrigin (nav_node_t *node, vec3_t origin)
{
	VectorCopy (node->origin, origin);
	origin[2] += 24.0f;
}

static qboolean Nav_BoxIntersectsLine (vec3_t mins, vec3_t maxs, vec3_t start, vec3_t end)
{
	float	enter = 0.0f, leave = 1.0f;
	float	rd, t0, t1, tmin, tmax;
	int		i;

	for (i = 0; i < 3; i++)
	{
		rd = 1.0f / (end[i] - start[i]);
		t0 = (mins[i] - start[i]) * rd;
		t1 = (maxs[i] - start[i]) * rd;
		tmin = t0 < t1 ? t0 : t1;
		tmax = t0 < t1 ? t1 : t0;
		if (tmin > enter)
			enter = tmin;
		if (tmax < leave)
			leave = tmax;
	}

	return enter < leave;
}

static void Nav_UpdateConditionalNode (nav_node_t *node)
{
	trace_t		tr;
	vec3_t		mins, maxs, origin, end, absmin, absmax;
	edict_t		*e;
	nav_link_t	*link;
	int			i;

	node->flags &= ~NodeFlag_Disabled;

	Nav_GetNodeBounds (node, mins, maxs);
	Nav_GetNodeTraceOrigin (node, origin);

	if (node->flags & NodeFlag_CheckInSolid)
	{
		tr = gi.trace (origin, mins, maxs, origin, NULL, MASK_SOLID);
		if (tr.startsolid || tr.allsolid)
		{
			node->flags |= NodeFlag_Disabled;
			return;
		}
	}

	if (node->flags & NodeFlag_CheckInLiquid)
	{
		tr = gi.trace (origin, mins, maxs, origin, NULL, MASK_WATER);
		if (!(tr.startsolid || tr.allsolid))
		{
			node->flags |= NodeFlag_Disabled;
			return;
		}
	}

	if (node->flags & NodeFlag_CheckForHazard)
	{
		tr = gi.trace (origin, mins, maxs, origin, NULL, CONTENTS_SLIME | CONTENTS_LAVA);
		if (tr.startsolid || tr.allsolid)
		{
			node->flags |= NodeFlag_Disabled;
			return;
		}

		VectorAdd (origin, mins, absmin);
		VectorAdd (origin, maxs, absmax);

		for (i = game.maxclients + 1; i < globals.num_edicts; i++)
		{
			e = &g_edicts[i];
			if (!e->inuse)
				continue;

			if (e->flags & FL_LASER_FIELD)
			{
				if (e->svflags & SVF_NOCLIENT)
					continue;
				if (Nav_BoxIntersectsLine (absmin, absmax, e->s.old_origin, e->s.origin))
				{
					node->flags |= NodeFlag_Disabled;
					return;
				}
			}
			else if (e->flags & FL_TRAP)
			{
				if (absmin[0] <= e->absmax[0] && absmax[0] >= e->absmin[0]
					&& absmin[1] <= e->absmax[1] && absmax[1] >= e->absmin[1]
					&& absmin[2] <= e->absmax[2] && absmax[2] >= e->absmin[2])
				{
					node->flags |= NodeFlag_Disabled;
					return;
				}
			}
		}
	}

	if (node->flags & NodeFlag_CheckHasFloor)
	{
		mins[2] = maxs[2] = 0;
		VectorCopy (origin, end);
		end[2] -= NavFloorDistance;
		tr = gi.trace (origin, mins, maxs, end, NULL, MASK_SOLID);
		if (tr.fraction == 1.0f)
		{
			node->flags |= NodeFlag_Disabled;
			return;
		}
	}

	if (node->flags & NodeFlag_CheckDoorLinks)
	{
		for (i = 0; i < node->num_links; i++)
		{
			link = &node->links[i];
			if (!link->edict)
				continue;
			e = link->edict->game_edict;
			if (!e || !e->inuse || e->s.modelindex != link->edict->model)
				continue;
			if (e->flags & FL_LOCKED)
			{
				node->flags |= NodeFlag_Disabled;
				return;
			}
		}
	}
}

// the movers the links are over, once the level has spawned and settled
static void Nav_SetupEntities (void)
{
	nav_edict_t	*e;
	edict_t		*game_e;
	int			i, n;

	for (i = 0; i < nav.num_edicts; i++)
	{
		e = &nav.edicts[i];

		for (n = game.maxclients + 1; n < globals.num_edicts; n++)
		{
			game_e = &g_edicts[n];
			if (!game_e->inuse)
				continue;
			if (game_e->solid != SOLID_TRIGGER && game_e->solid != SOLID_BSP)
				continue;
			if (game_e->s.modelindex == e->model)
			{
				e->game_edict = game_e;
				break;
			}
		}

		if (!e->game_edict)
			gi.dprintf ("Nav entity %i appears to be missing (needs entity with model %i)\n", i, e->model);
	}
}

/*
=============
Nav_Frame

Each frame: the nodes that something there may shut.
=============
*/
void Nav_Frame (void)
{
	int		i;

	if (!nav.nodes)
		return;

	if (!nav.setup_entities && level.time >= nav.setup_time)
	{
		Nav_SetupEntities ();
		nav.setup_entities = true;
	}

	for (i = 0; i < nav.num_conditional_nodes; i++)
		Nav_UpdateConditionalNode (nav.conditional_nodes[i]);
}
