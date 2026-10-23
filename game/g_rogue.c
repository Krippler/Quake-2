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

/*
g_rogue.c -- Ground Zero's weapons that its monsters fire, and what its
monsters need of its game besides, from its code (rogue/src, Yamagi's),
for the monsters of it that Call of the Machine has.
*/

#include "g_local.h"

/*
=================
The green blaster, blaster2: the daedalus's, the medic commander's, the
turret's; a bolt of 5 or more also bursts
=================
*/
void blaster2_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	normal;
	int		damagestat;

	if (other == self->owner)
		return;
	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}
	if (self->owner && self->owner->client)
		PlayerNoise (self->owner, self->s.origin, PNOISE_IMPACT);

	if (plane)
		VectorCopy (plane->normal, normal);
	else
		VectorCopy (vec3_origin, normal);

	if (other->takedamage)
	{
		// the bolt's burst does not hurt the one that fired it
		damagestat = self->owner ? self->owner->takedamage : 0;
		if (self->owner)
			self->owner->takedamage = DAMAGE_NO;
		if (self->dmg >= 5)
			T_RadiusDamage (self, self->owner, self->dmg * 3, other, self->dmg_radius, 0);
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, normal,
			self->dmg, 1, DAMAGE_ENERGY, MOD_BLASTER2);
		if (self->owner)
			self->owner->takedamage = damagestat;
	}
	else
	{
		if (self->dmg >= 5)
			T_RadiusDamage (self, self->owner, self->dmg * 3, self->owner, self->dmg_radius, 0);
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BLASTER2);
		gi.WritePosition (self->s.origin);
		gi.WriteDir (normal);
		gi.multicast (self->s.origin, MULTICAST_PVS);
	}
	G_FreeEdict (self);
}

void fire_blaster2 (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect)
{
	edict_t	*bolt;
	trace_t	tr;

	VectorNormalize (dir);

	bolt = G_Spawn ();
	VectorCopy (start, bolt->s.origin);
	VectorCopy (start, bolt->s.old_origin);
	vectoangles (dir, bolt->s.angles);
	VectorScale (dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	if (effect)
		bolt->s.effects |= EF_TRACKER;
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->dmg_radius = 128;
	bolt->s.modelindex = gi.modelindex ("models/proj/laser2/tris.md2");
	bolt->touch = blaster2_touch;
	bolt->owner = self;
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	gi.linkentity (bolt);

	tr = gi.trace (self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA (bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch (bolt, tr.ent, NULL, NULL);
	}
}

void monster_fire_blaster2 (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int flashtype, int effect)
{
	fire_blaster2 (self, start, dir, damage, speed, effect);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (flashtype);
	gi.multicast (start, MULTICAST_PVS);
}

/*
 * Ground Zero's vectoangles2: vectoangles without its rounding to whole
 * degrees, which its turret's aim needs.
 */
void vectoangles2 (vec3_t value, vec3_t angles)
{
	float	yaw, pitch, forward;

	if (value[1] == 0 && value[0] == 0)
	{
		yaw = 0;
		pitch = (value[2] > 0) ? 90 : 270;
	}
	else
	{
		if (value[0])
			yaw = atan2(value[1], value[0]) * 180 / M_PI;
		else if (value[1] > 0)
			yaw = 90;
		else
			yaw = 270;
		if (yaw < 0)
			yaw += 360;

		forward = sqrt(value[0]*value[0] + value[1]*value[1]);
		pitch = atan2(value[2], forward) * 180 / M_PI;
		if (pitch < 0)
			pitch += 360;
	}

	angles[PITCH] = -pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0;
}

/*
 * Ground Zero's stationary monsters (its turret): no dropping to the
 * floor, and its own trigger spawn that doesn't drop either.
 */
void monster_start_go (edict_t *self);
void monster_use (edict_t *self, edict_t *other, edict_t *activator);
qboolean monster_start (edict_t *self);

static void stationarymonster_triggered_spawn (edict_t *self)
{
	KillBox (self);

	self->solid = SOLID_BBOX;
	self->movetype = MOVETYPE_NONE;
	self->svflags &= ~SVF_NOCLIENT;
	self->air_finished = level.time + 12;
	gi.linkentity (self);

	monster_start_go (self);

	if (self->enemy && !(self->spawnflags & 1) && !(self->enemy->flags & FL_NOTARGET))
		FoundTarget (self);
	else
		self->enemy = NULL;
}

static void stationarymonster_triggered_spawn_use (edict_t *self, edict_t *other, edict_t *activator)
{
	// a frame's delay, so as not to telefrag whoever triggered it
	self->think = stationarymonster_triggered_spawn;
	self->nextthink = level.time + FRAMETIME;
	if (activator->client)
		self->enemy = activator;
	self->use = monster_use;
}

static void stationarymonster_triggered_start (edict_t *self)
{
	self->solid = SOLID_NOT;
	self->movetype = MOVETYPE_NONE;
	self->svflags |= SVF_NOCLIENT;
	self->nextthink = 0;
	self->use = stationarymonster_triggered_spawn_use;
}

static void stationarymonster_start_go (edict_t *self)
{
	if (!self->yaw_speed)
		self->yaw_speed = 20;

	if (self->spawnflags & 2)
		stationarymonster_triggered_start (self);
	else
		monster_start_go (self);
}

void stationarymonster_start (edict_t *self)
{
	self->think = stationarymonster_start_go;
	monster_start (self);
}

/*
 * Ground Zero's summoning (its medic commander and carrier): finding room
 * for a monster, the spawn-grow effect it appears in, and spawning it.
 */
#define	SPAWN_STEPSIZE		18
#define	SPAWNGROW_LIFESPAN	0.3

qboolean FindSpawnPoint (vec3_t startpoint, vec3_t mins, vec3_t maxs, vec3_t spawnpoint, float maxMoveUp)
{
	trace_t	tr;
	vec3_t	top;

	tr = gi.trace (startpoint, mins, maxs, startpoint, NULL, MASK_MONSTERSOLID|CONTENTS_PLAYERCLIP);
	if (tr.startsolid || tr.allsolid || tr.ent != world)
	{
		VectorCopy (startpoint, top);
		top[2] += maxMoveUp;

		tr = gi.trace (top, mins, maxs, startpoint, NULL, MASK_MONSTERSOLID);
		if (tr.startsolid || tr.allsolid)
			return false;
		VectorCopy (tr.endpos, spawnpoint);
		return true;
	}

	VectorCopy (startpoint, spawnpoint);
	return true;
}

qboolean CheckSpawnPoint (vec3_t origin, vec3_t mins, vec3_t maxs)
{
	trace_t	tr;

	if (!mins || !maxs || VectorCompare (mins, vec3_origin) || VectorCompare (maxs, vec3_origin))
		return false;

	tr = gi.trace (origin, mins, maxs, origin, NULL, MASK_MONSTERSOLID);
	if (tr.startsolid || tr.allsolid)
		return false;
	if (tr.ent != world)
		return false;
	return true;
}

// is there ground under it, near enough, flat enough and not water?
qboolean CheckGroundSpawnPoint (vec3_t origin, vec3_t entMins, vec3_t entMaxs, float height, float gravity)
{
	trace_t	tr;
	vec3_t	start, stop, mins, maxs;
	int		x, y;
	float	mid, bottom;

	if (!CheckSpawnPoint (origin, entMins, entMaxs))
		return false;

	VectorCopy (origin, stop);
	stop[2] = origin[2] + entMins[2] - height;

	tr = gi.trace (origin, entMins, entMaxs, stop, NULL, MASK_MONSTERSOLID|MASK_WATER);
	if (tr.fraction == 1 || !(tr.contents & MASK_MONSTERSOLID))
		return false;		// water, or nothing near enough

	VectorAdd (tr.endpos, entMins, mins);
	VectorAdd (tr.endpos, entMaxs, maxs);

	// the easy check: solid under all four corners
	if (gravity > 0)
		start[2] = maxs[2] + 1;
	else
		start[2] = mins[2] - 1;

	for (x = 0; x <= 1; x++)
		for (y = 0; y <= 1; y++)
		{
			start[0] = x ? maxs[0] : mins[0];
			start[1] = y ? maxs[1] : mins[1];
			if (gi.pointcontents (start) != CONTENTS_SOLID)
				goto realcheck;
		}
	return true;

realcheck:
	start[0] = stop[0] = (mins[0] + maxs[0]) * 0.5;
	start[1] = stop[1] = (mins[1] + maxs[1]) * 0.5;
	start[2] = mins[2];

	tr = gi.trace (start, vec3_origin, vec3_origin, stop, NULL, MASK_MONSTERSOLID);
	if (tr.fraction == 1.0)
		return false;

	if (gravity < 0)
	{
		start[2] = mins[2];
		stop[2] = start[2] - SPAWN_STEPSIZE - SPAWN_STEPSIZE;
		mid = bottom = tr.endpos[2] + entMins[2];
	}
	else
	{
		start[2] = maxs[2];
		stop[2] = start[2] + SPAWN_STEPSIZE + SPAWN_STEPSIZE;
		mid = bottom = tr.endpos[2] - entMaxs[2];
	}

	for (x = 0; x <= 1; x++)
		for (y = 0; y <= 1; y++)
		{
			start[0] = stop[0] = x ? maxs[0] : mins[0];
			start[1] = stop[1] = y ? maxs[1] : mins[1];

			tr = gi.trace (start, vec3_origin, vec3_origin, stop, NULL, MASK_MONSTERSOLID);
			if (gravity > 0)
			{
				if (tr.fraction != 1.0 && tr.endpos[2] < bottom)
					bottom = tr.endpos[2];
				if (tr.fraction == 1.0 || tr.endpos[2] - mid > SPAWN_STEPSIZE)
					return false;
			}
			else
			{
				if (tr.fraction != 1.0 && tr.endpos[2] > bottom)
					bottom = tr.endpos[2];
				if (tr.fraction == 1.0 || mid - tr.endpos[2] > SPAWN_STEPSIZE)
					return false;
			}
		}

	return true;		// it can land on it
}

// a monster's bounding box, from spawning one and freeing it
static void DetermineBBox (char *classname, vec3_t mins, vec3_t maxs)
{
	edict_t	*ent;

	ent = G_Spawn ();
	ent->classname = classname;
	ent->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
	ED_CallSpawn (ent);
	VectorCopy (ent->mins, mins);
	VectorCopy (ent->maxs, maxs);
	G_FreeEdict (ent);
}

edict_t *CreateMonster (vec3_t origin, vec3_t angles, char *classname)
{
	edict_t	*ent;

	ent = G_Spawn ();
	VectorCopy (origin, ent->s.origin);
	VectorCopy (angles, ent->s.angles);
	ent->classname = classname;
	ent->monsterinfo.aiflags |= AI_DO_NOT_COUNT;
	ED_CallSpawn (ent);
	ent->s.renderfx |= RF_IR_VISIBLE;
	return ent;
}

edict_t *CreateFlyMonster (vec3_t origin, vec3_t angles, vec3_t mins, vec3_t maxs, char *classname)
{
	vec3_t	bmins, bmaxs;

	if (!mins || !maxs || VectorCompare (mins, vec3_origin) || VectorCompare (maxs, vec3_origin))
	{
		DetermineBBox (classname, bmins, bmaxs);
		mins = bmins;
		maxs = bmaxs;
	}
	if (!CheckSpawnPoint (origin, mins, maxs))
		return NULL;
	return CreateMonster (origin, angles, classname);
}

edict_t *CreateGroundMonster (vec3_t origin, vec3_t angles, vec3_t entMins, vec3_t entMaxs, char *classname, int height)
{
	vec3_t	mins, maxs;

	if (!entMins || !entMaxs || VectorCompare (entMins, vec3_origin) || VectorCompare (entMaxs, vec3_origin))
		DetermineBBox (classname, mins, maxs);
	else
	{
		VectorCopy (entMins, mins);
		VectorCopy (entMaxs, maxs);
	}

	// ground under it, flat, and not toxic
	if (!CheckGroundSpawnPoint (origin, mins, maxs, height, -1))
		return NULL;
	return CreateMonster (origin, angles, classname);
}

static void spawngrow_think (edict_t *self)
{
	self->s.angles[0] = rand() % 360;
	self->s.angles[1] = rand() % 360;
	self->s.angles[2] = rand() % 360;

	if (level.time < self->wait && self->s.frame < 2)
		self->s.frame++;
	if (level.time >= self->wait)
	{
		if (self->s.effects & EF_SPHERETRANS)
		{
			G_FreeEdict (self);
			return;
		}
		else if (self->s.frame > 0)
			self->s.frame--;
		else
		{
			G_FreeEdict (self);
			return;
		}
	}
	self->nextthink += FRAMETIME;
}

void SpawnGrow_Spawn (vec3_t startpos, int size)
{
	edict_t	*ent;
	float	lifespan;

	ent = G_Spawn ();
	VectorCopy (startpos, ent->s.origin);
	ent->s.angles[0] = rand() % 360;
	ent->s.angles[1] = rand() % 360;
	ent->s.angles[2] = rand() % 360;
	ent->solid = SOLID_NOT;
	ent->s.renderfx = RF_IR_VISIBLE;
	ent->movetype = MOVETYPE_NONE;
	ent->classname = "spawngro";

	if (size <= 1)
	{
		lifespan = SPAWNGROW_LIFESPAN;
		ent->s.modelindex = gi.modelindex ("models/items/spawngro2/tris.md2");
	}
	else if (size == 2)
	{
		ent->s.modelindex = gi.modelindex ("models/items/spawngro3/tris.md2");
		lifespan = 2;
	}
	else
	{
		ent->s.modelindex = gi.modelindex ("models/items/spawngro/tris.md2");
		lifespan = SPAWNGROW_LIFESPAN;
	}

	ent->think = spawngrow_think;
	ent->wait = level.time + lifespan;
	ent->nextthink = level.time + FRAMETIME;
	if (size != 2)
		ent->s.effects |= EF_SPHERETRANS;
	gi.linkentity (ent);
}

// in coop, a visible player at random for a summoned monster to go for
edict_t *PickCoopTarget (edict_t *self)
{
	edict_t	*targets[MAX_CLIENTS];
	edict_t	*ent;
	int		num_targets = 0, id, player;

	if (!coop->value)
		return NULL;

	for (player = 1; player <= game.maxclients && num_targets < MAX_CLIENTS; player++)
	{
		ent = &g_edicts[player];
		if (!ent->inuse || !ent->client)
			continue;
		if (visible (self, ent))
			targets[num_targets++] = ent;
	}
	if (!num_targets)
		return NULL;

	id = random() * num_targets;
	if (id == num_targets)
		id--;
	return targets[id];
}

// the distance between their origins
float realrange (edict_t *self, edict_t *other)
{
	vec3_t	dir;

	VectorSubtract (self->s.origin, other->s.origin, dir);
	return VectorLength (dir);
}
