/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// m_move.c -- monster movement

#include "g_local.h"

#define	STEPSIZE	18

/*
=============
M_CheckBottom

Returns false if any part of the bottom of the entity is off an edge that
is not a staircase.

=============
*/
int c_yes, c_no;

qboolean M_CheckBottom (edict_t *ent)
{
	vec3_t	mins, maxs, start, stop;
	trace_t	trace;
	int		x, y;
	float	mid, bottom;
	
	VectorAdd (ent->s.origin, ent->mins, mins);
	VectorAdd (ent->s.origin, ent->maxs, maxs);

// if all of the points under the corners are solid world, don't bother
// with the tougher checks
// the corners must be within 16 of the midpoint
	start[2] = GRAVITY_UP(ent) ? maxs[2] + 1 : mins[2] - 1;
	for	(x=0 ; x<=1 ; x++)
		for	(y=0 ; y<=1 ; y++)
		{
			start[0] = x ? maxs[0] : mins[0];
			start[1] = y ? maxs[1] : mins[1];
			if (gi.pointcontents (start) != CONTENTS_SOLID)
				goto realcheck;
		}

	c_yes++;
	return true;		// we got out easy

realcheck:
	c_no++;
//
// check it for real...
//
	start[2] = mins[2];
	
// the midpoint must be within 16 of the bottom
	start[0] = stop[0] = (mins[0] + maxs[0])*0.5;
	start[1] = stop[1] = (mins[1] + maxs[1])*0.5;
	stop[2] = start[2] - 2*STEPSIZE;
	if (GRAVITY_UP(ent))		// Ground Zero's stalker: the ceiling
	{
		start[2] = maxs[2];
		stop[2] = start[2] + 2*STEPSIZE;
	}
	trace = gi.trace (start, vec3_origin, vec3_origin, stop, ent, MASK_MONSTERSOLID);

	if (trace.fraction == 1.0)
		return false;
	mid = bottom = trace.endpos[2];
	
// the corners must be within 16 of the midpoint	
	for	(x=0 ; x<=1 ; x++)
		for	(y=0 ; y<=1 ; y++)
		{
			start[0] = stop[0] = x ? maxs[0] : mins[0];
			start[1] = stop[1] = y ? maxs[1] : mins[1];
			
			trace = gi.trace (start, vec3_origin, vec3_origin, stop, ent, MASK_MONSTERSOLID);
			
			if (GRAVITY_UP(ent))
			{
				if (trace.fraction != 1.0 && trace.endpos[2] < bottom)
					bottom = trace.endpos[2];
				if (trace.fraction == 1.0 || trace.endpos[2] - mid > STEPSIZE)
					return false;
				continue;
			}
			if (trace.fraction != 1.0 && trace.endpos[2] > bottom)
				bottom = trace.endpos[2];
			if (trace.fraction == 1.0 || mid - trace.endpos[2] > STEPSIZE)
				return false;
		}

	c_yes++;
	return true;
}


/*
=============
SV_movestep

Called by monster program code.
The move will be adjusted for slopes and stairs, but if the move isn't
possible, no move is done, false is returned, and
pr_global_struct->trace_normal is set to the normal of the blocking wall
=============
*/
//FIXME since we need to test end position contents here, can we avoid doing
//it again later in catagorize position?
edict_t *CheckForBadArea (edict_t *ent);
void TargetTesla (edict_t *self, edict_t *tesla);

// Ground Zero's: is the move toward the bad area, which is behind it?
static qboolean IsBadAhead (edict_t *self, edict_t *bad, vec3_t move)
{
	vec3_t	dir, forward, move_copy;
	float	dp_bad, dp_move;

	VectorCopy (move, move_copy);
	VectorSubtract (bad->s.origin, self->s.origin, dir);
	VectorNormalize (dir);
	AngleVectors (self->s.angles, forward, NULL, NULL);
	dp_bad = DotProduct (forward, dir);

	VectorNormalize (move_copy);
	dp_move = DotProduct (forward, move_copy);

	if (dp_bad < 0 && dp_move < 0)
		return true;
	if (dp_bad > 0 && dp_move > 0)
		return true;
	return false;
}

qboolean SV_movestep (edict_t *ent, vec3_t move, qboolean relink)
{
	float		dz;
	vec3_t		oldorg, neworg, end;
	trace_t		trace;
	int			i;
	float		stepsize;
	vec3_t		test;
	int			contents;
	edict_t		*current_bad = NULL, *new_bad;

	// Ground Zero's bad areas (a tesla's field)
	if (ent->health > 0)
	{
		current_bad = CheckForBadArea (ent);
		if (current_bad)
		{
			ent->bad_area = current_bad;

			// facing the tesla in it: back up
			if (ent->enemy && !strcmp (ent->enemy->classname, "tesla") && IsBadAhead (ent, current_bad, move))
				VectorScale (move, -1, move);
		}
		else if (ent->bad_area)
		{
			// out of it: back to business
			ent->bad_area = NULL;
			if (ent->oldenemy)
			{
				ent->enemy = ent->oldenemy;
				ent->goalentity = ent->oldenemy;
				FoundTarget (ent);
				return true;
			}
		}
	}

// try the move	
	VectorCopy (ent->s.origin, oldorg);
	VectorAdd (ent->s.origin, move, neworg);

// flying monsters don't step up
	if ( ent->flags & (FL_SWIM | FL_FLY) )
	{
		// the re-release's flight sets its velocity, which moves it
		if ((ent->monsterinfo.aiflags & AI_ALTERNATE_FLY) && SV_alternate_flystep (ent))
			return true;

	// try one move with vertical motion, then one without
		for (i=0 ; i<2 ; i++)
		{
			VectorAdd (ent->s.origin, move, neworg);
			if (i == 0 && ent->enemy)
			{
				if (!ent->goalentity)
					ent->goalentity = ent->enemy;
				// the re-release's: the height its path is at
				if (ent->monsterinfo.aiflags & AI_PATHING)
					dz = ent->s.origin[2] - ent->monsterinfo.nav_path.firstMovePoint[2];
				else
					dz = ent->s.origin[2] - ent->goalentity->s.origin[2];
				if (ent->goalentity->client)
				{
					if (dz > 40)
						neworg[2] -= 8;
					if (!((ent->flags & FL_SWIM) && (ent->waterlevel < 2)))
						if (dz < 30)
							neworg[2] += 8;
				}
				else
				{
					if (dz > 8)
						neworg[2] -= 8;
					else if (dz > 0)
						neworg[2] -= dz;
					else if (dz < -8)
						neworg[2] += 8;
					else
						neworg[2] += dz;
				}
			}
			trace = gi.trace (ent->s.origin, ent->mins, ent->maxs, neworg, ent, MASK_MONSTERSOLID);
	
			// fly monsters don't enter water voluntarily
			if (ent->flags & FL_FLY)
			{
				if (!ent->waterlevel)
				{
					test[0] = trace.endpos[0];
					test[1] = trace.endpos[1];
					test[2] = trace.endpos[2] + ent->mins[2] + 1;
					contents = gi.pointcontents(test);
					if (contents & MASK_WATER)
						return false;
				}
			}

			// swim monsters don't exit water voluntarily
			if (ent->flags & FL_SWIM)
			{
				if (ent->waterlevel < 2)
				{
					test[0] = trace.endpos[0];
					test[1] = trace.endpos[1];
					test[2] = trace.endpos[2] + ent->mins[2] + 1;
					contents = gi.pointcontents(test);
					if (!(contents & MASK_WATER))
						return false;
				}
			}

			if (trace.fraction == 1)
			{
				VectorCopy (trace.endpos, ent->s.origin);
				// not into a bad area
				if (!current_bad && CheckForBadArea (ent))
					VectorCopy (oldorg, ent->s.origin);
				else
				{
					if (relink)
					{
						gi.linkentity (ent);
						G_TouchTriggers (ent);
					}
					return true;
				}
			}
			
			if (!ent->enemy)
				break;
		}
		
		return false;
	}

// push down from a step height above the wished position
	if (!(ent->monsterinfo.aiflags & AI_NOSTEP))
		stepsize = STEPSIZE;
	else
		stepsize = 1;

	// from a step above to a step below, "above" being down on a ceiling
	if (GRAVITY_UP(ent))
		neworg[2] -= stepsize;
	else
		neworg[2] += stepsize;
	VectorCopy (neworg, end);
	end[2] += GRAVITY_UP(ent) ? stepsize*2 : -stepsize*2;

	trace = gi.trace (neworg, ent->mins, ent->maxs, end, ent, MASK_MONSTERSOLID);

	if (trace.allsolid)
		return false;

	if (trace.startsolid)
	{
		neworg[2] -= stepsize;
		trace = gi.trace (neworg, ent->mins, ent->maxs, end, ent, MASK_MONSTERSOLID);
		if (trace.allsolid || trace.startsolid)
			return false;
	}


	// don't go in to water
	if (ent->waterlevel == 0)
	{
		test[0] = trace.endpos[0];
		test[1] = trace.endpos[1];
		if (GRAVITY_UP(ent))
			test[2] = trace.endpos[2] + ent->maxs[2] - 1;
		else
			test[2] = trace.endpos[2] + ent->mins[2] + 1;	
		contents = gi.pointcontents(test);

		if (contents & MASK_WATER)
			return false;
	}

	if (trace.fraction == 1)
	{
	// if monster had the ground pulled out, go ahead and fall
		if ( ent->flags & FL_PARTIALGROUND )
		{
			VectorAdd (ent->s.origin, move, ent->s.origin);
			if (relink)
			{
				gi.linkentity (ent);
				G_TouchTriggers (ent);
			}
			ent->groundentity = NULL;
			return true;
		}
	
		return false;		// walked off an edge
	}

// check point traces down for dangling corners
	VectorCopy (trace.endpos, ent->s.origin);

	// Ground Zero's: not into a bad area, but at a tesla blocking it
	if (ent->health > 0)
	{
		new_bad = CheckForBadArea (ent);
		if (!current_bad && new_bad)
		{
			if (new_bad->owner && new_bad->owner->classname && !strcmp (new_bad->owner->classname, "tesla"))
			{
				// one already angry at a tesla is left alone
				if (!ent->enemy || !ent->enemy->inuse
					|| (strcmp (ent->enemy->classname, "tesla")
					&& (!ent->enemy->client || !visible (ent, ent->enemy))))
				{
					TargetTesla (ent, new_bad->owner);
					ent->monsterinfo.aiflags |= AI_BLOCKED;
				}
			}
			VectorCopy (oldorg, ent->s.origin);
			return false;
		}
	}
	
	if (!M_CheckBottom (ent))
	{
		if ( ent->flags & FL_PARTIALGROUND )
		{	// entity had floor mostly pulled out from underneath it
			// and is trying to correct
			if (relink)
			{
				gi.linkentity (ent);
				G_TouchTriggers (ent);
			}
			return true;
		}
		VectorCopy (oldorg, ent->s.origin);
		return false;
	}

	if ( ent->flags & FL_PARTIALGROUND )
	{
		ent->flags &= ~FL_PARTIALGROUND;
	}
	ent->groundentity = trace.ent;
	ent->groundentity_linkcount = trace.ent->linkcount;

// the move is ok
	if (relink)
	{
		gi.linkentity (ent);
		G_TouchTriggers (ent);
	}
	return true;
}


//============================================================================

/*
===============
M_ChangeYaw

===============
*/
void M_ChangeYaw (edict_t *ent)
{
	float	ideal;
	float	current;
	float	move;
	float	speed;
	
	current = anglemod(ent->s.angles[YAW]);
	ideal = ent->ideal_yaw;

	if (current == ideal)
		return;

	move = ideal - current;
	speed = ent->yaw_speed;
	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
			move = move + 360;
	}
	if (move > 0)
	{
		if (move > speed)
			move = speed;
	}
	else
	{
		if (move < -speed)
			move = -speed;
	}
	
	ent->s.angles[YAW] = anglemod (current + move);
}


/*
======================
SV_StepDirection

Turns to the movement direction, and walks the current distance if
facing it.

======================
*/
qboolean SV_StepDirection (edict_t *ent, float yaw, float dist)
{
	vec3_t		move, oldorigin;
	float		delta;
	
	ent->ideal_yaw = yaw;
	M_ChangeYaw (ent);
	
	yaw = yaw*M_PI*2 / 360;
	move[0] = cos(yaw)*dist;
	move[1] = sin(yaw)*dist;
	move[2] = 0;

	VectorCopy (ent->s.origin, oldorigin);
	if (SV_movestep (ent, move, false))
	{
		delta = ent->s.angles[YAW] - ent->ideal_yaw;
		if (delta > 45 && delta < 315)
		{		// not turned far enough, so don't take the step
			VectorCopy (oldorigin, ent->s.origin);
		}
		gi.linkentity (ent);
		G_TouchTriggers (ent);
		return true;
	}
	gi.linkentity (ent);
	G_TouchTriggers (ent);
	return false;
}

/*
======================
SV_FixCheckBottom

======================
*/
void SV_FixCheckBottom (edict_t *ent)
{
	ent->flags |= FL_PARTIALGROUND;
}



/*
================
SV_NewChaseDir

================
*/
#define	DI_NODIR	-1
static qboolean SV_NewChaseDirPos (edict_t *actor, vec3_t pos, float dist)
{
	float	deltax,deltay;
	float	d[3];
	float	tdir, olddir, turnaround;

	olddir = anglemod( (int)(actor->ideal_yaw/45)*45 );
	turnaround = anglemod(olddir - 180);

	deltax = pos[0] - actor->s.origin[0];
	deltay = pos[1] - actor->s.origin[1];
	if (deltax>10)
		d[1]= 0;
	else if (deltax<-10)
		d[1]= 180;
	else
		d[1]= DI_NODIR;
	if (deltay<-10)
		d[2]= 270;
	else if (deltay>10)
		d[2]= 90;
	else
		d[2]= DI_NODIR;

// try direct route
	if (d[1] != DI_NODIR && d[2] != DI_NODIR)
	{
		if (d[1] == 0)
			tdir = d[2] == 90 ? 45 : 315;
		else
			tdir = d[2] == 90 ? 135 : 215;
			
		if (tdir != turnaround && SV_StepDirection(actor, tdir, dist))
			return true;
	}

// try other directions
	if ( ((rand()&3) & 1) ||  abs(deltay)>abs(deltax))
	{
		tdir=d[1];
		d[1]=d[2];
		d[2]=tdir;
	}

	if (d[1]!=DI_NODIR && d[1]!=turnaround 
	&& SV_StepDirection(actor, d[1], dist))
			return true;

	if (d[2]!=DI_NODIR && d[2]!=turnaround
	&& SV_StepDirection(actor, d[2], dist))
			return true;

	// Ground Zero's: a monster that can do something else when blocked
	if (actor->monsterinfo.blocked && actor->inuse && actor->health > 0
		&& actor->monsterinfo.blocked (actor, dist))
		return true;

/* there is no direct path to the player, so pick another direction */

	if (olddir!=DI_NODIR && SV_StepDirection(actor, olddir, dist))
			return true;

	if (rand()&1) 	/*randomly determine direction of search*/
	{
		for (tdir=0 ; tdir<=315 ; tdir += 45)
			if (tdir!=turnaround && SV_StepDirection(actor, tdir, dist) )
					return true;
	}
	else
	{
		for (tdir=315 ; tdir >=0 ; tdir -= 45)
			if (tdir!=turnaround && SV_StepDirection(actor, tdir, dist) )
					return true;
	}

	if (turnaround != DI_NODIR && SV_StepDirection(actor, turnaround, dist) )
			return true;

	actor->ideal_yaw = olddir;		// can't move

// if a bridge was pulled out from underneath a monster, it may not have
// a valid standing position at all

	if (!M_CheckBottom (actor))
		SV_FixCheckBottom (actor);
	return false;
}

void SV_NewChaseDir (edict_t *actor, edict_t *enemy, float dist)
{
	//FIXME: how did we get here with no enemy
	if (!enemy)
		return;

	SV_NewChaseDirPos (actor, enemy->s.origin, dist);
}

/*
======================
SV_CloseEnough

======================
*/
qboolean SV_CloseEnough (edict_t *ent, edict_t *goal, float dist)
{
	int		i;
	
	for (i=0 ; i<3 ; i++)
	{
		if (goal->absmin[i] > ent->absmax[i] + dist)
			return false;
		if (goal->absmax[i] < ent->absmin[i] - dist)
			return false;
	}
	return true;
}


/*
======================
M_MoveToGoal
======================
*/
/*
 * The re-release's pathing on its navigation nodes (g_nav.c), on a map
 * that has them: a monster that can't see its enemy, or is too far from
 * it, goes by the path the nodes give.
 */

// as SV_StepDirection, but put back as it was if it can't, and a turn
// without the step is not a failure
static qboolean SV_StepDirectionPath (edict_t *ent, float yaw, float dist)
{
	vec3_t	move, oldorigin;
	float	old_ideal_yaw, old_yaw;

	if (!ent->inuse)
		return true;

	old_ideal_yaw = ent->ideal_yaw;
	old_yaw = ent->s.angles[YAW];

	ent->ideal_yaw = yaw;
	M_ChangeYaw (ent);

	yaw = yaw*M_PI*2 / 360;
	move[0] = cos(yaw)*dist;
	move[1] = sin(yaw)*dist;
	move[2] = 0;

	VectorCopy (ent->s.origin, oldorigin);
	if (SV_movestep (ent, move, false))
	{
		if (!ent->inuse)
			return true;

		if (strncmp (ent->classname, "monster_widow", 13) && !FacingIdeal (ent))
		{	// not turned far enough, so don't take the step, but still turn
			VectorCopy (oldorigin, ent->s.origin);
			M_CheckGround (ent);
			return true;
		}
		gi.linkentity (ent);
		G_TouchTriggers (ent);
		return true;
	}
	gi.linkentity (ent);
	G_TouchTriggers (ent);
	ent->ideal_yaw = old_ideal_yaw;
	ent->s.angles[YAW] = old_yaw;
	return false;
}

static qboolean M_NavPathToGoal (edict_t *self, float dist)
{
	nav_request_t	request;
	vec3_t			ground_origin, box_min, box_max, d;
	float			*path_to;
	float			yaw, old_yaw, old_ideal_yaw;
	nav_info_t		*path = &self->monsterinfo.nav_path;

	// mark us as *trying* now (nav_path is valid)
	self->monsterinfo.aiflags |= AI_PATHING;

	// a player's box where it stands, to tell it reached its point
	VectorCopy (self->s.origin, ground_origin);
	ground_origin[2] += self->mins[2] + 24;
	VectorSet (box_min, ground_origin[0] - 16, ground_origin[1] - 16, ground_origin[2] - 24);
	VectorSet (box_max, ground_origin[0] + 16, ground_origin[1] + 16, ground_origin[2] + 32);
	path_to = path->firstMovePoint;

	if (self->monsterinfo.nav_path_cache_time <= level.time
		|| (path->returnCode != PathReturnCode_TraversalPending
		&& path_to[0] >= box_min[0] && path_to[0] <= box_max[0]
		&& path_to[1] >= box_min[1] && path_to[1] <= box_max[1]
		&& path_to[2] >= box_min[2] && path_to[2] <= box_max[2]))
	{
		memset (&request, 0, sizeof(request));
		if (self->enemy)
			VectorCopy (self->enemy->s.origin, request.goal);
		else
			VectorCopy (self->goalentity->s.origin, request.goal);
		request.moveDist = dist;
		VectorCopy (self->s.origin, request.start);
		request.pathFlags = PathFlags_Walk;

		request.minHeight = -(self->mins[2] * 2);
		request.maxHeight = self->maxs[2] * 2;
		if (!strcmp (self->classname, "monster_guardian"))
			request.radius = 2048;
		else
			request.radius = 512;

		if (self->monsterinfo.can_jump || (self->flags & FL_FLY))
		{
			if (self->monsterinfo.jump_height)
			{
				request.pathFlags |= PathFlags_BarrierJump;
				request.jumpHeight = self->monsterinfo.jump_height;
			}
			if (self->monsterinfo.drop_height)
			{
				request.pathFlags |= PathFlags_WalkOffLedge;
				request.dropHeight = self->monsterinfo.drop_height;
			}
		}

		if (self->flags & FL_FLY)
		{
			request.maxHeight = request.minHeight = 8192;
			request.pathFlags |= PathFlags_LongJump;
		}

		if (!Nav_GetPathToGoal (&request, path))
		{
			// fatal error, don't bother ever trying nodes
			if (path->returnCode == PathReturnCode_NoNavAvailable)
				self->monsterinfo.no_path_finding = true;
			return false;
		}

		self->monsterinfo.nav_path_cache_time = level.time + 2;
	}

	old_yaw = self->s.angles[YAW];
	old_ideal_yaw = self->ideal_yaw;

	path_to = (path->returnCode == PathReturnCode_TraversalPending) ?
		path->secondMovePoint : path->firstMovePoint;

	if (self->monsterinfo.random_change_time >= level.time && !(self->monsterinfo.aiflags & AI_ALTERNATE_FLY))
		yaw = self->ideal_yaw;
	else
	{
		VectorSubtract (path_to, self->s.origin, d);
		yaw = vectoyaw (d);
	}

	if (!SV_StepDirectionPath (self, yaw, dist))
	{
		if (!self->inuse)
			return false;

		if (self->monsterinfo.blocked && !(self->monsterinfo.aiflags & AI_TARGET_ANGER) && self->health > 0)
		{
			// if we're blocked, the blocked function will be deferred to for yaw
			self->s.angles[YAW] = old_yaw;
			self->ideal_yaw = old_ideal_yaw;
			if (self->monsterinfo.blocked (self, dist))
				return true;
		}

		// try the first point
		if (self->monsterinfo.random_change_time >= level.time)
			yaw = self->ideal_yaw;
		else
		{
			VectorSubtract (path->firstMovePoint, self->s.origin, d);
			yaw = vectoyaw (d);
		}

		if (!SV_StepDirectionPath (self, yaw, dist))
		{
			// we got blocked, but all is not lost yet; do a similar bump
			// around-ish behavior to try to regain our composure
			if (self->monsterinfo.random_change_time < level.time && self->inuse)
			{
				self->monsterinfo.random_change_time = level.time + 1.5;
				if (SV_NewChaseDirPos (self, path_to, dist))
					return true;
			}

			self->monsterinfo.path_blocked_counter += FRAMETIME * 3;
		}

		if (self->monsterinfo.path_blocked_counter > 1.5)
			return false;
	}

	return true;
}

// melee-only monsters close right in; the others to where they can fire
static qboolean M_MeleeStyle (edict_t *self)
{
	if (!strcmp (self->classname, "monster_berserk") || !strcmp (self->classname, "monster_mutant"))
		return true;
	return !self->monsterinfo.attack && self->monsterinfo.melee;
}

static qboolean M_MoveToPath (edict_t *self, float dist)
{
	gclient_t	*cl;
	float		dz, h;

	if (!Nav_Available ())
		return false;
	if (self->monsterinfo.no_path_finding)
		return false;
	if (!strcmp (self->classname, "monster_turret"))
		return false;
	if (self->monsterinfo.path_wait_time > level.time)
		return false;
	if (!self->enemy)
		return false;
	cl = self->enemy->client;
	if (cl && cl->invisible_framenum > level.framenum && cl->invisibility_fade_framenum <= level.framenum)
		return false;
	if (self->monsterinfo.attack_state >= AS_MISSILE)
		return true;

	dz = fabs (self->s.origin[2] - self->enemy->s.origin[2]);
	h = self->maxs[2] > -self->mins[2] ? self->maxs[2] : -self->mins[2];

	if (visible (self, self->enemy))
	{
		if (self->flags & (FL_SWIM | FL_FLY))
			return false;	// the normal "shoot, walk, shoot"
		if (M_MeleeStyle (self))
		{
			// path pretty close to the enemy, then let normal Quake
			// movement take over
			if (range_to (self, self->enemy) > 240 || dz > h)
			{
				if (M_NavPathToGoal (self, dist))
					return true;
			}
			else
				return false;
		}
		else
		{
			// most mixed combat AI have fairly short range attacks, so try
			// to path within mid range
			if (range_to (self, self->enemy) > 440 || dz > h * 2)
			{
				if (M_NavPathToGoal (self, dist))
					return true;
			}
			else
				return false;
		}
	}
	else
	{
		// we can't see our enemy, let's see if we can path to them
		if (M_NavPathToGoal (self, dist))
			return true;
	}

	if (!self->inuse)
		return false;

	if (self->monsterinfo.nav_path.returnCode > PathReturnCode_StartPathErrors)
	{
		self->monsterinfo.path_wait_time = level.time + 10;
		return false;
	}

	self->monsterinfo.path_blocked_counter += FRAMETIME * 3;

	if (self->monsterinfo.path_blocked_counter > 5)
	{
		self->monsterinfo.path_blocked_counter = 0;
		self->monsterinfo.path_wait_time = level.time + 5;
		return false;
	}

	return true;
}

void M_MoveToGoal (edict_t *ent, float dist)
{
	edict_t		*goal;
	
	goal = ent->goalentity;

	if (!ent->groundentity && !(ent->flags & (FL_FLY|FL_SWIM)))
		return;

	// the re-release's: by its paths, where it can't see its enemy
	if (goal && !(ent->monsterinfo.aiflags & AI_COMBAT_POINT) && ent->monsterinfo.attack_state < AS_MISSILE)
	{
		if (M_MoveToPath (ent, dist))
		{
			ent->monsterinfo.path_blocked_counter -= FRAMETIME;
			if (ent->monsterinfo.path_blocked_counter < 0)
				ent->monsterinfo.path_blocked_counter = 0;
			return;
		}
	}
	ent->monsterinfo.aiflags &= ~AI_PATHING;

	// the re-release's flight steers itself to where it would be
	if (ent->monsterinfo.aiflags & AI_ALTERNATE_FLY)
	{
		if (goal)
			SV_StepDirection (ent, ent->ideal_yaw, dist);
		return;
	}

// if the next step hits the enemy, return immediately
	if (ent->enemy &&  SV_CloseEnough (ent, ent->enemy, dist) )
		return;

// bump around...
	if ( (rand()&3)==1 || !SV_StepDirection (ent, ent->ideal_yaw, dist))
	{
		if (ent->inuse)
			SV_NewChaseDir (ent, goal, dist);
	}
}


/*
===============
M_walkmove
===============
*/
qboolean M_walkmove (edict_t *ent, float yaw, float dist)
{
	vec3_t	move;
	
	if (!ent->groundentity && !(ent->flags & (FL_FLY|FL_SWIM)))
		return false;

	yaw = yaw*M_PI*2 / 360;
	
	move[0] = cos(yaw)*dist;
	move[1] = sin(yaw)*dist;
	move[2] = 0;

	return SV_movestep(ent, move, true);
}
