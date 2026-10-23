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
