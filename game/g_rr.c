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
