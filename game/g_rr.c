// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

/*
 * The re-release's (GPL-2, ZeniMax) helpers its monsters need, in the
 * 1997 API: Call of the Machine's gunner commander, shambler and arachnid.
 */

#include "g_local.h"

// the distance between two boxes, 0 if they touch
float distance_between_boxes (vec3_t absminsa, vec3_t absmaxsa, vec3_t absminsb, vec3_t absmaxsb)
{
	float	len = 0, d;
	int		i;

	for (i = 0; i < 3; i++)
	{
		if (absmaxsa[i] < absminsb[i])
		{
			d = absmaxsa[i] - absminsb[i];
			len += d * d;
		}
		else if (absminsa[i] > absmaxsb[i])
		{
			d = absminsa[i] - absmaxsb[i];
			len += d * d;
		}
	}
	return sqrt(len);
}

float range_to (edict_t *self, edict_t *other)
{
	return distance_between_boxes (self->absmin, self->absmax, other->absmin, other->absmax);
}

static void closest_point_to_box (vec3_t from, vec3_t absmins, vec3_t absmaxs, vec3_t out)
{
	int		i;

	for (i = 0; i < 3; i++)
		out[i] = (from[i] < absmins[i]) ? absmins[i] : (from[i] > absmaxs[i]) ? absmaxs[i] : from[i];
}

/*
The re-release's fire_hit: its reach is from box to box, as its monsters'
melee is tuned for (MELEE_DISTANCE_RR); the base one's is from the origin.
*/
qboolean fire_hit_rr (edict_t *self, vec3_t aim, int damage, int kick)
{
	trace_t	tr;
	vec3_t	forward, right, up, v, point, dir;
	float	range;

	range = distance_between_boxes (self->enemy->absmin, self->enemy->absmax, self->absmin, self->absmax);
	if (range > aim[0])
		return false;

	if (!(aim[1] > self->mins[0] && aim[1] < self->maxs[0]))
	{
		// a side hit: the "right" value out to the edge of its box
		if (aim[1] < 0)
			aim[1] = self->enemy->mins[0];
		else
			aim[1] = self->enemy->maxs[0];
	}

	closest_point_to_box (self->s.origin, self->enemy->absmin, self->enemy->absmax, point);

	// can it hit the point on the box?
	tr = gi.trace (self->s.origin, NULL, NULL, point, self, MASK_SHOT);
	if (tr.fraction < 1)
	{
		if (!tr.ent->takedamage)
			return false;
		// any client or monster it would hit is the one it wanted
		if ((tr.ent->svflags & SVF_MONSTER) || tr.ent->client)
			tr.ent = self->enemy;
	}

	// and the enemy from the point?
	tr = gi.trace (point, NULL, NULL, self->enemy->s.origin, self, MASK_SHOT);
	if (tr.fraction < 1)
	{
		if (!tr.ent->takedamage)
			return false;
		if ((tr.ent->svflags & SVF_MONSTER) || tr.ent->client)
			tr.ent = self->enemy;
	}

	AngleVectors (self->s.angles, forward, right, up);
	VectorMA (self->s.origin, range, forward, point);
	VectorMA (point, aim[1], right, point);
	VectorMA (point, aim[2], up, point);
	VectorSubtract (point, self->enemy->s.origin, dir);

	T_Damage (tr.ent, self, self, dir, point, vec3_origin, damage, kick/2, DAMAGE_NO_KNOCKBACK, MOD_HIT);

	if (!(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client)
		return false;

	// its own knockback
	VectorAdd (self->enemy->absmin, self->enemy->absmax, v);
	VectorScale (v, 0.5, v);
	VectorSubtract (v, point, v);
	VectorNormalize (v);
	VectorMA (self->enemy->velocity, kick, v, self->enemy->velocity);
	if (self->enemy->velocity[2] > 0)
		self->enemy->groundentity = NULL;
	return true;
}

/*
The re-release's (and Ground Zero's) aim: where the target will be when a
bolt of bolt_speed reaches it (0 is instant), less offset seconds; at its
eyes, or its origin when the other is blocked.
*/
void PredictAim (edict_t *self, edict_t *target, vec3_t start, float bolt_speed, qboolean eye_height, float offset, vec3_t aimdir, vec3_t aimpoint)
{
	vec3_t	dir, vec, a, b;
	float	dist, time;
	trace_t	tr;

	if (!target || !target->inuse)
	{
		if (aimdir)
			VectorClear (aimdir);
		return;
	}

	VectorSubtract (target->s.origin, start, dir);
	if (eye_height)
		dir[2] += target->viewheight;
	dist = VectorLength (dir);

	// if this one is blocked, try the other
	VectorAdd (start, dir, vec);
	tr = gi.trace (start, NULL, NULL, vec, self, MASK_SHOT);
	if (tr.ent != target)
	{
		eye_height = !eye_height;
		VectorSubtract (target->s.origin, start, dir);
		if (eye_height)
			dir[2] += target->viewheight;
		dist = VectorLength (dir);
	}

	time = bolt_speed ? dist / bolt_speed : 0;

	VectorMA (target->s.origin, time - offset, target->velocity, vec);

	// went backwards...
	VectorCopy (dir, a);
	VectorNormalize (a);
	VectorSubtract (vec, start, b);
	VectorNormalize (b);
	if (DotProduct (a, b) < 0)
		VectorCopy (target->s.origin, vec);
	else
	{
		// a shot the prediction puts into a nearby wall goes straight
		tr = gi.trace (start, NULL, NULL, vec, NULL, MASK_SOLID);
		if (tr.fraction < 0.9)
			VectorCopy (target->s.origin, vec);
	}

	if (eye_height)
		vec[2] += target->viewheight;

	if (aimdir)
	{
		VectorSubtract (vec, start, aimdir);
		VectorNormalize (aimdir);
	}
	if (aimpoint)
		VectorCopy (vec, aimpoint);
}

// would a shot from the offset reach its enemy?  start is where it's from
qboolean M_CheckClearShot (edict_t *self, vec3_t offset, vec3_t start)
{
	vec3_t	f, r, real_angles, target;
	trace_t	tr;
	qboolean	is_blind;

	if (!self->enemy)
		return false;

	real_angles[0] = self->s.angles[0];
	real_angles[1] = self->ideal_yaw;
	real_angles[2] = 0;
	AngleVectors (real_angles, f, r, NULL);
	G_ProjectSource (self->s.origin, offset, f, r, start);

	is_blind = self->monsterinfo.attack_state == AS_BLIND
		|| (self->monsterinfo.aiflags & (AI_MANUAL_STEERING|AI_LOST_SIGHT));

	if (is_blind)
		VectorCopy (self->monsterinfo.blind_fire_target, target);
	else
	{
		VectorCopy (self->enemy->s.origin, target);
		target[2] += self->enemy->viewheight;
	}

	tr = gi.trace (start, NULL, NULL, target, self, MASK_SHOT & ~CONTENTS_DEADMONSTER);
	if (tr.ent == self->enemy || tr.ent->client || (tr.fraction > 0.8 && !tr.startsolid))
		return true;

	if (!is_blind)
	{
		VectorCopy (self->enemy->s.origin, target);
		tr = gi.trace (start, NULL, NULL, target, self, MASK_SHOT & ~CONTENTS_DEADMONSTER);
		if (tr.ent == self->enemy || tr.ent->client || (tr.fraction > 0.8 && !tr.startsolid))
			return true;
	}

	return false;
}

// the re-release's end of a death animation
void monster_dead (edict_t *self)
{
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0;
	gi.linkentity (self);
}

void monster_muzzleflash (edict_t *self, vec3_t start, int flashtype)
{
	// the re-release's past a byte in a message of their own
	gi.WriteByte (flashtype > 255 ? svc_muzzleflash3 : svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	if (flashtype > 255)
		gi.WriteShort (flashtype);
	else
		gi.WriteByte (flashtype);
	gi.multicast (start, MULTICAST_PVS);
}

// would a step of dist straight ahead succeed?
qboolean ai_check_move (edict_t *self, float dist)
{
	float	yaw = self->s.angles[YAW] * M_PI * 2 / 360;
	vec3_t	move, old_origin;

	move[0] = cos(yaw) * dist;
	move[1] = sin(yaw) * dist;
	move[2] = 0;

	VectorCopy (self->s.origin, old_origin);
	if (!SV_movestep (self, move, false))
		return false;

	VectorCopy (old_origin, self->s.origin);
	gi.linkentity (self);
	return true;
}

/*
A pitch that will at some point land a lob on or near the target: each
is tried by simulating the lob for time_remaining.  aim is set to it.
*/
qboolean M_CalculatePitchToFire (edict_t *self, vec3_t target, vec3_t start, vec3_t aim, float speed, float time_remaining, qboolean mortar, qboolean destroy_on_touch)
{
	static const float	pitches[] = { -80, -70, -60, -50, -40, -30, -20, -10, -5 };
	const float	sim_time = 0.1;
	float		best_pitch = 0, best_dist = -1, t, dist;
	vec3_t		pitched_aim, fwd, velocity, origin, end, v;
	trace_t		tr;
	int			i;

	vectoangles2 (aim, pitched_aim);

	for (i = 0; i < (int)(sizeof(pitches) / sizeof(pitches[0])); i++)
	{
		if (mortar && pitches[i] >= -30)
			break;

		pitched_aim[PITCH] = pitches[i];
		AngleVectors (pitched_aim, fwd, NULL, NULL);

		VectorScale (fwd, speed, velocity);
		VectorCopy (start, origin);

		for (t = time_remaining; t > 0; t -= sim_time)
		{
			velocity[2] -= sv_gravity->value * sim_time;

			VectorMA (origin, sim_time, velocity, end);
			tr = gi.trace (origin, NULL, NULL, end, NULL, MASK_SHOT);
			VectorCopy (tr.endpos, origin);

			if (tr.fraction < 1.0)
			{
				if (tr.surface && (tr.surface->flags & SURF_SKY))
					break;

				VectorAdd (origin, tr.plane.normal, origin);
				ClipVelocity (velocity, tr.plane.normal, velocity, 1.6);

				VectorSubtract (origin, target, v);
				dist = DotProduct (v, v);

				if (tr.ent == self->enemy || tr.ent->client
					|| (tr.plane.normal[2] >= 0.7 && dist < 128*128 && (best_dist < 0 || dist < best_dist)))
				{
					best_pitch = pitches[i];
					best_dist = dist;
				}

				if (destroy_on_touch || (tr.contents & (CONTENTS_MONSTER|CONTENTS_DEADMONSTER)))
					break;
			}
		}
	}

	if (best_dist >= 0)
	{
		pitched_aim[PITCH] = best_pitch;
		AngleVectors (pitched_aim, aim, NULL, NULL);
		return true;
	}

	return false;
}

/*
The re-release's slam: damage and a knock up to all on the ground about
the point, falling off with the square of distance.
*/
void T_SlamRadiusDamage (vec3_t point, edict_t *inflictor, edict_t *attacker, float damage, float kick, edict_t *ignore, float radius, int mod)
{
	edict_t	*ent = NULL;
	vec3_t	v, dir, mins, maxs, closest;
	float	amount, points;
	int		i;

	while ((ent = findradius (ent, inflictor->s.origin, radius * 2)) != NULL)
	{
		if (ent == ignore)
			continue;
		if (!ent->takedamage)
			continue;
		if (!CanDamage (ent, inflictor))
			continue;
		// not players in mid air
		if (ent->client && !ent->groundentity)
			continue;

		VectorAdd (ent->s.origin, ent->mins, mins);
		VectorAdd (ent->s.origin, ent->maxs, maxs);
		for (i = 0; i < 3; i++)
			closest[i] = point[i] < mins[i] ? mins[i] : point[i] > maxs[i] ? maxs[i] : point[i];
		VectorSubtract (closest, point, v);

		amount = 1 - VectorLength (v) / radius;
		if (amount > 1)
			amount = 1;
		if (amount <= 0)
			continue;		// too far away
		amount *= amount;

		points = damage * amount;
		if (points < 1)
			points = 1;

		VectorSubtract (ent->s.origin, point, dir);
		VectorNormalize (dir);

		// the point at their feet, so they're always knocked up
		point[2] = ent->absmin[2];
		T_Damage (ent, inflictor, attacker, dir, point, dir, (int)points, (int)(kick * amount), DAMAGE_RADIUS, mod);

		if (ent->client && ent->velocity[2] < 270)
			ent->velocity[2] = 270;
	}
}

/*
The re-release's (and Ground Zero's) ducking
*/
#define	DUCK_INTERVAL	5

void monster_duck_down (edict_t *self)
{
	self->monsterinfo.aiflags |= AI_DUCKED;

	self->maxs[2] = self->monsterinfo.base_height - 32;
	self->takedamage = DAMAGE_AIM;
	self->monsterinfo.next_duck_time = level.time + DUCK_INTERVAL;
	gi.linkentity (self);
}

void monster_duck_hold (edict_t *self)
{
	if (level.time >= self->monsterinfo.duck_wait_time)
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
	else
		self->monsterinfo.aiflags |= AI_HOLD_FRAME;
}

void monster_duck_up (edict_t *self)
{
	if (!(self->monsterinfo.aiflags & AI_DUCKED))
		return;

	self->monsterinfo.aiflags &= ~AI_DUCKED;
	self->maxs[2] = self->monsterinfo.base_height;
	self->takedamage = DAMAGE_AIM;
	// a duck up done: the time till the next halved
	if (self->monsterinfo.next_duck_time > level.time)
		self->monsterinfo.next_duck_time = level.time + (self->monsterinfo.next_duck_time - level.time) / 2;
	gi.linkentity (self);
}

// the re-release's per-skill chance: easy, normal, hard, nightmare
static qboolean G_SkillCheck (float easy, float normal, float hard, float nightmare)
{
	float	chance;
	int		s = (int)skill->value;

	chance = s <= 0 ? easy : s == 1 ? normal : s == 2 ? hard : nightmare;
	return chance == 1.0 ? true : random() < chance;
}

/*
The re-release's dodge: sidestep, or duck a shot that will hit high, by
what the monster has for each.  tr is where the shot will hit, if known.
*/
void M_MonsterDodge (edict_t *self, edict_t *attacker, float eta, trace_t *tr)
{
	float		r = random();
	float		height;
	qboolean	ducker = false, dodger = false;

	// it may have "died" already
	if (self->health < 1)
		return;

	if (self->monsterinfo.duck && self->monsterinfo.unduck)
		ducker = true;
	if (self->monsterinfo.sidestep && !(self->monsterinfo.aiflags & AI_STAND_GROUND))
		dodger = true;

	if (!ducker && !dodger)
		return;

	if (!self->enemy)
	{
		self->enemy = attacker;
		FoundTarget (self);
	}

	// not if it will hit anyway, or is far off
	if (eta < FRAMETIME || eta > 2.5)
		return;

	// skill level determination
	if (r > 0.5)
		return;

	if (ducker && tr)
	{
		height = self->absmax[2] - 32 - 1;		// absmax is s.origin + maxs + 1

		if (!dodger && (tr->endpos[2] <= height || (self->monsterinfo.aiflags & AI_DUCKED)))
			return;
	}
	else
		height = self->absmax[2];

	if (dodger)
	{
		// already dodging: finish that
		if (self->monsterinfo.aiflags & AI_DODGING)
			return;

		// ducked already, or the shot is at its knees
		if (!ducker || !tr || tr->endpos[2] <= height || (self->monsterinfo.aiflags & AI_DUCKED))
		{
			// not as often on easy and normal
			if (!G_SkillCheck (0.25, 0.5, 1, 1))
			{
				self->monsterinfo.dodge_time = level.time + 0.8 + random() * 0.6;
				return;
			}

			if (tr)
			{
				vec3_t	right, diff;

				AngleVectors (self->s.angles, NULL, right, NULL);
				VectorSubtract (tr->endpos, self->s.origin, diff);
				self->monsterinfo.lefty = DotProduct (right, diff) >= 0;
			}
			else
				self->monsterinfo.lefty = rand() & 1;

			if (self->monsterinfo.sidestep (self))
			{
				if (ducker && (self->monsterinfo.aiflags & AI_DUCKED))
					self->monsterinfo.unduck (self);

				self->monsterinfo.aiflags |= AI_DODGING;
				self->monsterinfo.attack_state = AS_SLIDING;
				self->monsterinfo.dodge_time = level.time + 0.4 + random() * 1.6;
			}
			return;
		}
	}

	// duck only shots about to hit
	if (ducker && tr && eta < 0.5)
	{
		if (self->monsterinfo.next_duck_time > level.time)
			return;

		monster_done_dodge (self);

		if (self->monsterinfo.duck (self, eta))
		{
			if (self->monsterinfo.duck_wait_time < level.time)
				self->monsterinfo.duck_wait_time = level.time + eta;

			monster_duck_down (self);

			// on easy and normal, longer
			if (skill->value == 0)
				self->monsterinfo.duck_wait_time += 0.5 + random() * 0.5;
			else if (skill->value == 1)
				self->monsterinfo.duck_wait_time += 0.1 + random() * 0.25;
		}

		self->monsterinfo.dodge_time = level.time + 0.2 + random() * 0.5;
	}
}

/*
The re-release's reinforcements: a summoner's list of "classname strength"
pairs, ';' between, and how much strength it may have out at once.  Each
is spawned and freed once to learn its box.
*/
// the next word, ' ' or ';' after it; NULL at the end
static char *RR_Token (char **p, char *out, int size)
{
	int		len = 0;

	while (**p == ' ' || **p == ';')
		(*p)++;
	if (!**p)
		return NULL;
	while (**p && **p != ' ' && **p != ';')
	{
		if (len < size - 1)
			out[len++] = **p;
		(*p)++;
	}
	out[len] = 0;
	return out;
}

void M_SetupReinforcements (edict_t *self, char *reinforcements, int default_slots)
{
	edict_t	*ent;
	char	*p, *token, number[16];
	int		n;
	spawn_temp_t	saved;

	self->monsterinfo.monster_slots = st.monster_slots ? st.monster_slots : default_slots;
	self->monsterinfo.num_reinforcements = 0;
	memset (self->monsterinfo.chosen_reinforcements, 255, sizeof(self->monsterinfo.chosen_reinforcements));

	if (st.reinforcements)
		reinforcements = st.reinforcements;
	if (!self->monsterinfo.monster_slots || !reinforcements || !*reinforcements)
		return;

	if (skill->value)
		self->monsterinfo.monster_slots += floor(self->monsterinfo.monster_slots * (skill->value / 2));

	// the summoner's spawn keys are not theirs
	saved = st;
	memset (&st, 0, sizeof(st));

	p = reinforcements;
	while (self->monsterinfo.num_reinforcements < MAX_REINFORCEMENT_TYPES)
	{
		n = self->monsterinfo.num_reinforcements;

		// "classname strength", ';' or ' ' between
		token = RR_Token (&p, self->monsterinfo.reinforcements[n].classname, sizeof(self->monsterinfo.reinforcements[n].classname));
		if (!token)
			break;
		token = RR_Token (&p, number, sizeof(number));
		self->monsterinfo.reinforcements[n].strength = token ? atoi (number) : 0;

		ent = G_Spawn ();
		ent->classname = self->monsterinfo.reinforcements[n].classname;
		ent->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
		ED_CallSpawn (ent);
		VectorCopy (ent->mins, self->monsterinfo.reinforcements[n].mins);
		VectorCopy (ent->maxs, self->monsterinfo.reinforcements[n].maxs);
		G_FreeEdict (ent);

		self->monsterinfo.num_reinforcements++;
	}

	st = saved;
}

int M_SlotsLeft (edict_t *self)
{
	return self->monsterinfo.monster_slots - self->monsterinfo.monster_used;
}

/*
Ready (in chosen_reinforcements) up to max_slots of them (0 for no
limit), as much as there's room for; how many, on a logarithmic scale
so it's seldom many.  Returns how many.
*/
int M_PickReinforcements (edict_t *self, int max_slots)
{
	byte	available[MAX_REINFORCEMENT_TYPES];
	int		num_slots, remaining, num_chosen, num_available, i;

	memset (self->monsterinfo.chosen_reinforcements, 255, sizeof(self->monsterinfo.chosen_reinforcements));

	num_slots = (int)(log(random() * (1 << MAX_REINFORCEMENTS)) / log(2));
	if (num_slots < 1)
		num_slots = 1;

	remaining = M_SlotsLeft (self);

	for (num_chosen = 0; num_chosen < num_slots; num_chosen++)
	{
		if ((max_slots && num_chosen == max_slots) || remaining <= 0)
			break;

		// what there's room for
		num_available = 0;
		for (i = 0; i < self->monsterinfo.num_reinforcements; i++)
			if (self->monsterinfo.reinforcements[i].strength <= remaining)
				available[num_available++] = i;
		if (!num_available)
			break;

		i = rand() % num_available;
		self->monsterinfo.chosen_reinforcements[num_chosen] = available[i];
		remaining -= self->monsterinfo.reinforcements[available[i]].strength;
	}

	return num_chosen;
}

/*
=============
RR_Map

Whether the level is one of the re-release's own, which its player moves
through as it does (qcommon/pmove.c): the engine says, as it loads it.
=============
*/
qboolean RR_Map (void)
{
	static cvar_t	*sv_rerelease_map;

	if (!sv_rerelease_map)
		sv_rerelease_map = gi.cvar ("sv_rerelease_map", "0", 0);
	return sv_rerelease_map->value != 0;
}

/*
=============
ThrowGibs

The re-release's list of a monster's gibs: so many of each, in its own
skin where it says, its head last, which the monster itself becomes.
=============
*/
void ThrowGibs (edict_t *self, int damage, gib_def_t *gibs)
{
	edict_t	*gib;
	int		i, n, skin;

	for ( ; gibs->gibname ; gibs++)
		for (n = gibs->count ? gibs->count : 1, i = 0 ; i < n ; i++)
		{
			if (gibs->type & GIB_HEAD)
			{
				skin = self->s.skinnum;
				ThrowHead (self, gibs->gibname, damage, gibs->type & GIB_METALLIC);
				if (gibs->type & GIB_SKINNED)
					self->s.skinnum = skin;
				self->monsterinfo.setskin = NULL;
				continue;
			}
			gib = ThrowGib (self, gibs->gibname, damage, gibs->type & GIB_METALLIC);
			if (gib && (gibs->type & GIB_SKINNED))
				gib->s.skinnum = self->s.skinnum;
		}
}

// the re-release's: dead enough to be gibbed, or crushed once dead
qboolean M_CheckGib (edict_t *self)
{
	if (self->deadflag && meansOfDeath == MOD_CRUSH)
		return true;
	return self->health <= self->gib_health;
}

// the re-release's: whether pain shows, not ducked or on a combat point,
// nor in nightmare unless it is a chainfist
qboolean M_ShouldReactToPain (edict_t *self)
{
	if (self->monsterinfo.aiflags & (AI_DUCKED | AI_COMBAT_POINT))
		return false;
	return meansOfDeath == MOD_CHAINFIST || skill->value < 3;
}

// the re-release's: a monster's step, which its client sounds by the
// material stepped on (client/cl_fx.c)
void monster_footstep (edict_t *self)
{
	if (self->groundentity)
		self->s.event = EV_OTHER_FOOTSTEP;
}

/*
=============
blocked_checkjump_rr

The re-release's jump, when a monster is blocked going after its enemy:
down to where it stands, no more than drop_height, onto level ground and
not into deep water; or up onto a ledge no higher than jump_height,
facing the wall first. Not again till jump_time. It has no navigation
paths here.
=============
*/
qboolean face_wall (edict_t *self);

void monster_jump_start_rr (edict_t *self)
{
	monster_done_dodge (self);
	self->monsterinfo.jump_time = level.time + 3;
}

// landed, or long enough; and still going forward
qboolean monster_jump_finished_rr (edict_t *self)
{
	vec3_t	forward, v;
	float	z;

	AngleVectors (self->s.angles, forward, NULL, NULL);
	VectorSet (v, self->velocity[0] * forward[0], self->velocity[1] * forward[1], self->velocity[2] * forward[2]);
	if (VectorLength (v) < 150)
	{
		z = self->velocity[2];
		VectorScale (forward, 150, self->velocity);
		self->velocity[2] = z;
	}

	return self->monsterinfo.jump_time < level.time;
}

#define	STEPSIZE_RR	18

int blocked_checkjump_rr (edict_t *self, float dist)
{
	int		playerPosition;
	trace_t	trace, deep;
	vec3_t	pt1, pt2, forward, up, mid;

	// can't jump even if we physically can, or no enemy to go after
	if (!self->monsterinfo.can_jump || !self->enemy)
		return JUMP_NO;

	// we just jumped recently, don't try again
	if (self->monsterinfo.jump_time > level.time)
		return JUMP_NO;

	AngleVectors (self->s.angles, forward, NULL, up);

	if (self->enemy->absmin[2] > self->absmin[2] + STEPSIZE_RR)
		playerPosition = 1;
	else if (self->enemy->absmin[2] < self->absmin[2] - STEPSIZE_RR)
		playerPosition = -1;
	else
		playerPosition = 0;

	if (playerPosition == -1 && self->monsterinfo.drop_height)
	{
		// can it even get to the spot it is going to fall from?
		VectorMA (self->s.origin, 48, forward, pt1);
		trace = gi.trace (self->s.origin, self->mins, self->maxs, pt1, self, MASK_MONSTERSOLID);
		if (trace.fraction < 1)
			return JUMP_NO;

		VectorCopy (pt1, pt2);
		pt2[2] = self->absmin[2] - self->monsterinfo.drop_height - 1;

		trace = gi.trace (pt1, vec3_origin, vec3_origin, pt2, self, MASK_MONSTERSOLID | MASK_WATER);
		if (trace.fraction < 1 && !trace.allsolid && !trace.startsolid)
		{
			// how deep the water is: not over its waist
			if (trace.contents & CONTENTS_WATER)
			{
				deep = gi.trace (trace.endpos, vec3_origin, vec3_origin, pt2, self, MASK_MONSTERSOLID);
				VectorCopy (deep.endpos, mid);
				mid[2] += (self->mins[2] + self->maxs[2]) * 0.5 - self->mins[2];
				if (gi.pointcontents (mid) & MASK_WATER)
					return JUMP_NO;
			}

			if (self->absmin[2] - trace.endpos[2] >= 24 && (trace.contents & (MASK_SOLID | CONTENTS_WATER)))
			{
				if (self->enemy->absmin[2] - trace.endpos[2] > 32)
					return JUMP_NO;
				if (trace.plane.normal[2] < 0.9)
					return JUMP_NO;

				monster_jump_start_rr (self);
				return JUMP_DOWN;
			}
		}
	}
	else if (playerPosition == 1 && self->monsterinfo.jump_height)
	{
		VectorMA (self->s.origin, 48, forward, pt1);
		VectorCopy (pt1, pt2);
		pt1[2] = self->absmax[2] + self->monsterinfo.jump_height;

		trace = gi.trace (pt1, vec3_origin, vec3_origin, pt2, self, MASK_MONSTERSOLID | MASK_WATER);
		if (trace.fraction < 1 && !trace.allsolid && !trace.startsolid
			&& trace.endpos[2] - self->absmin[2] <= self->monsterinfo.jump_height
			&& (trace.contents & (MASK_SOLID | CONTENTS_WATER)))
		{
			face_wall (self);
			monster_jump_start_rr (self);
			return JUMP_UP;
		}
	}

	return JUMP_NO;
}
