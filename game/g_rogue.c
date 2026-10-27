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

	monster_muzzleflash (self, start, flashtype);
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

// spawn a monster mid-game, with none of the last map entity's spawn keys
static void RR_CallSpawnClean (edict_t *ent)
{
	spawn_temp_t	saved = st;

	memset (&st, 0, sizeof(st));
	ED_CallSpawn (ent);
	st = saved;
}
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
	RR_CallSpawnClean (ent);
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
	RR_CallSpawnClean (ent);
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

/*
 * Ground Zero's blocked monsters (its stalker): ride a plat, or jump, toward
 * an enemy above or below.
 */
#define	PLAT_STATE_TOP		0		// g_func.c's
#define	PLAT_STATE_BOTTOM	1

qboolean has_valid_enemy (edict_t *self)
{
	return self->enemy && self->enemy->inuse && self->enemy->health >= 1;
}

void monster_jump_start (edict_t *self)
{
	self->timestamp = level.time;
}

qboolean monster_jump_finished (edict_t *self)
{
	return level.time - self->timestamp > 3;
}

// turn to face the wall before it
qboolean face_wall (edict_t *self)
{
	vec3_t	pt, forward, ang;
	trace_t	tr;

	AngleVectors (self->s.angles, forward, NULL, NULL);
	VectorMA (self->s.origin, 64, forward, pt);
	tr = gi.trace (self->s.origin, vec3_origin, vec3_origin, pt, self, MASK_MONSTERSOLID);
	if (tr.fraction < 1 && !tr.allsolid && !tr.startsolid)
	{
		vectoangles2 (tr.plane.normal, ang);
		self->ideal_yaw = ang[YAW] + 180;
		if (self->ideal_yaw > 360)
			self->ideal_yaw -= 360;
		M_ChangeYaw (self);
		return true;
	}
	return false;
}

qboolean blocked_checkplat (edict_t *self, float dist)
{
	int		playerPosition;
	trace_t	trace;
	vec3_t	pt1, pt2, forward;
	edict_t	*plat;

	if (!self->enemy)
		return false;

	// the player's altitude, relative
	if (self->enemy->absmin[2] >= self->absmax[2])
		playerPosition = 1;
	else if (self->enemy->absmax[2] <= self->absmin[2])
		playerPosition = -1;
	else
		return false;		// near enough the same

	// a plat it's on, or will step on to
	plat = NULL;
	if (self->groundentity && self->groundentity != world
		&& !strncmp(self->groundentity->classname, "func_plat", 8))
		plat = self->groundentity;
	if (!plat)
	{
		AngleVectors (self->s.angles, forward, NULL, NULL);
		VectorMA (self->s.origin, dist, forward, pt1);
		VectorCopy (pt1, pt2);
		pt2[2] -= 384;
		trace = gi.trace (pt1, vec3_origin, vec3_origin, pt2, self, MASK_MONSTERSOLID);
		if (trace.fraction < 1 && !trace.allsolid && !trace.startsolid
			&& !strncmp(trace.ent->classname, "func_plat", 8))
			plat = trace.ent;
	}

	if (plat && plat->use)
	{
		if (playerPosition == 1)
		{
			if ((self->groundentity == plat && plat->moveinfo.state == PLAT_STATE_BOTTOM)
				|| (self->groundentity != plat && plat->moveinfo.state == PLAT_STATE_TOP))
			{
				plat->use (plat, self, self);
				return true;
			}
		}
		else
		{
			if ((self->groundentity == plat && plat->moveinfo.state == PLAT_STATE_TOP)
				|| (self->groundentity != plat && plat->moveinfo.state == PLAT_STATE_BOTTOM))
			{
				plat->use (plat, self, self);
				return true;
			}
		}
	}
	return false;
}

qboolean blocked_checkjump (edict_t *self, float dist, float maxDown, float maxUp)
{
	int		playerPosition;
	trace_t	trace;
	vec3_t	pt1, pt2, forward, up;

	if (!self->enemy)
		return false;

	AngleVectors (self->s.angles, forward, NULL, up);

	if (self->enemy->absmin[2] > self->absmin[2] + 16)
		playerPosition = 1;
	else if (self->enemy->absmin[2] < self->absmin[2] - 16)
		playerPosition = -1;
	else
		playerPosition = 0;

	if (playerPosition == -1 && maxDown)
	{
		// can it get to the spot it would drop from?
		VectorMA (self->s.origin, 48, forward, pt1);
		trace = gi.trace (self->s.origin, self->mins, self->maxs, pt1, self, MASK_MONSTERSOLID);
		if (trace.fraction < 1)
			return false;

		VectorCopy (pt1, pt2);
		pt2[2] = self->absmin[2] - maxDown - 1;
		trace = gi.trace (pt1, vec3_origin, vec3_origin, pt2, self, MASK_MONSTERSOLID|MASK_WATER);
		if (trace.fraction < 1 && !trace.allsolid && !trace.startsolid
			&& self->absmin[2] - trace.endpos[2] >= 24 && (trace.contents & MASK_SOLID))
		{
			if (self->enemy->absmin[2] - trace.endpos[2] > 32)
				return false;
			if (trace.plane.normal[2] < 0.9)
				return false;
			return true;
		}
	}
	else if (playerPosition == 1 && maxUp)
	{
		VectorMA (self->s.origin, 48, forward, pt1);
		VectorCopy (pt1, pt2);
		pt1[2] = self->absmax[2] + maxUp;
		trace = gi.trace (pt1, vec3_origin, vec3_origin, pt2, self, MASK_MONSTERSOLID|MASK_WATER);
		if (trace.fraction < 1 && !trace.allsolid && !trace.startsolid
			&& trace.endpos[2] - self->absmin[2] <= maxUp && (trace.contents & MASK_SOLID))
		{
			face_wall (self);
			return true;
		}
	}
	return false;
}

void monster_done_dodge (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_DODGING;
	// the re-release's: a sidestep's slide is over with it
	if (self->monsterinfo.attack_state == AS_SLIDING)
		self->monsterinfo.attack_state = AS_STRAIGHT;
}

/*
 * Ground Zero's flechettes (its ETF rifle's, and the re-release's gunner
 * commander's chaingun)
 */
static void flechette_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	dir, normal;

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
		VectorClear (normal);

	if (other->takedamage)
		T_Damage (other, self, self->owner, self->velocity, self->s.origin,
			normal, self->dmg, self->dmg_radius, DAMAGE_NO_REG_ARMOR, MOD_ETF_RIFLE);
	else
	{
		VectorScale (normal, 256, dir);
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_FLECHETTE);
		gi.WritePosition (self->s.origin);
		gi.WriteDir (dir);
		gi.multicast (self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict (self);
}

void fire_flechette (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int kick)
{
	edict_t	*flechette;

	VectorNormalize (dir);

	flechette = G_Spawn ();
	VectorCopy (start, flechette->s.origin);
	VectorCopy (start, flechette->s.old_origin);
	vectoangles2 (dir, flechette->s.angles);
	VectorScale (dir, speed, flechette->velocity);
	flechette->movetype = MOVETYPE_FLYMISSILE;
	flechette->clipmask = MASK_SHOT;
	flechette->solid = SOLID_BBOX;
	flechette->s.renderfx = RF_FULLBRIGHT;
	VectorClear (flechette->mins);
	VectorClear (flechette->maxs);
	flechette->s.modelindex = gi.modelindex ("models/proj/flechette/tris.md2");
	flechette->owner = self;
	flechette->touch = flechette_touch;
	flechette->nextthink = level.time + 8000.0 / speed;
	flechette->think = G_FreeEdict;
	flechette->dmg = damage;
	flechette->dmg_radius = kick;
	flechette->classname = "flechette";
	gi.linkentity (flechette);
}

void monster_fire_flechette (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int flashtype)
{
	fire_flechette (self, start, dir, damage, speed, damage / 2);

	monster_muzzleflash (self, start, flashtype);
}

// Ground Zero's: is other behind self, or straight below it?
qboolean inback (edict_t *self, edict_t *other)
{
	vec3_t	vec, forward;

	AngleVectors (self->s.angles, forward, NULL, NULL);
	VectorSubtract (other->s.origin, self->s.origin, vec);
	VectorNormalize (vec);
	return DotProduct (vec, forward) < -0.3;
}

qboolean below (edict_t *self, edict_t *other)
{
	vec3_t	vec, down = {0, 0, -1};

	VectorSubtract (other->s.origin, self->s.origin, vec);
	VectorNormalize (vec);
	return DotProduct (vec, down) > 0.95;		// an 18 degree arc below
}

// vectoyaw without its rounding to whole degrees
float vectoyaw2 (vec3_t vec)
{
	float	yaw;

	if (vec[PITCH] == 0)
	{
		if (vec[YAW] == 0)
			yaw = 0;
		else if (vec[YAW] > 0)
			yaw = 90;
		else
			yaw = 270;
	}
	else
	{
		yaw = atan2(vec[YAW], vec[PITCH]) * 180 / M_PI;
		if (yaw < 0)
			yaw += 360;
	}
	return yaw;
}

/*
 * Ground Zero's player melee (its chainfist): a hit within reach pulls the
 * player forward
 */
void fire_player_melee (edict_t *self, vec3_t start, vec3_t aim, int reach, int damage, int kick, int quiet, int mod)
{
	vec3_t	forward, right, up, v, point;
	trace_t	tr;

	vectoangles2 (aim, v);
	AngleVectors (v, forward, right, up);
	VectorNormalize (forward);
	VectorMA (start, reach, forward, point);

	// see if the hit connects
	tr = gi.trace (start, NULL, NULL, point, self, MASK_SHOT);
	if (tr.fraction == 1.0)
	{
		if (!quiet)
			gi.sound (self, CHAN_WEAPON, gi.soundindex ("weapons/swish.wav"), 1, ATTN_NORM, 0);
		return;
	}

	if (tr.ent->takedamage == DAMAGE_YES || tr.ent->takedamage == DAMAGE_AIM)
	{
		// pull the player forward for the damage done
		VectorMA (self->velocity, 75, forward, self->velocity);
		VectorMA (self->velocity, 75, up, self->velocity);

		if (mod == MOD_CHAINFIST)
			T_Damage (tr.ent, self, self, vec3_origin, tr.ent->s.origin, vec3_origin,
				damage, kick / 2, DAMAGE_DESTROY_ARMOR | DAMAGE_NO_KNOCKBACK, mod);
		else
			T_Damage (tr.ent, self, self, vec3_origin, tr.ent->s.origin, vec3_origin,
				damage, kick / 2, DAMAGE_NO_KNOCKBACK, mod);

		if (!quiet)
			gi.sound (self, CHAN_WEAPON, gi.soundindex ("weapons/meatht.wav"), 1, ATTN_NORM, 0);
	}
	else
	{
		if (!quiet)
			gi.sound (self, CHAN_WEAPON, gi.soundindex ("weapons/tink1.wav"), 1, ATTN_NORM, 0);

		VectorScale (tr.plane.normal, 256, point);
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_GUNSHOT);
		gi.WritePosition (tr.endpos);
		gi.WriteDir (point);
		gi.multicast (tr.endpos, MULTICAST_PVS);
	}
}

/*
 * Ground Zero's disruptor's tracker: a bolt that homes on what it was fired
 * at, and burns what it hits for half a second more
 */
#define	TRACKER_DAMAGE_FLAGS	(DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY | DAMAGE_NO_KNOCKBACK)
#define	TRACKER_IMPACT_FLAGS	(DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY)
#define	TRACKER_DAMAGE_TIME		0.5

static void tracker_pain_daemon_think (edict_t *self)
{
	static vec3_t	pain_normal = {0, 0, 1};
	int		hurt;

	if (!self->inuse)
		return;

	if (level.time - self->timestamp > TRACKER_DAMAGE_TIME || self->enemy->health <= 0)
	{
		if (!self->enemy->client)
			self->enemy->s.effects &= ~EF_TRACKERTRAIL;
		G_FreeEdict (self);
		return;
	}

	T_Damage (self->enemy, self, self->owner, vec3_origin, self->enemy->s.origin,
		pain_normal, self->dmg, 0, TRACKER_DAMAGE_FLAGS, MOD_TRACKER);

	// if it killed the player, it's gone
	if (!self->inuse)
		return;

	// a monster it killed it gibs
	if (self->enemy->health < 1)
	{
		hurt = self->enemy->gib_health ? -self->enemy->gib_health : 500;
		T_Damage (self->enemy, self, self->owner, vec3_origin, self->enemy->s.origin,
			pain_normal, hurt, 0, TRACKER_DAMAGE_FLAGS, MOD_TRACKER);
	}

	if (!self->enemy->client)
		self->enemy->s.effects |= EF_TRACKERTRAIL;

	self->nextthink = level.time + FRAMETIME;
}

static void tracker_pain_daemon_spawn (edict_t *owner, edict_t *enemy, int damage)
{
	edict_t	*daemon;

	daemon = G_Spawn ();
	daemon->classname = "pain daemon";
	daemon->think = tracker_pain_daemon_think;
	daemon->nextthink = level.time + FRAMETIME;
	daemon->timestamp = level.time;
	daemon->owner = owner;
	daemon->enemy = enemy;
	daemon->dmg = damage;
}

static void tracker_explode (edict_t *self)
{
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_TRACKER_EXPLOSION);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PVS);

	G_FreeEdict (self);
}

static void tracker_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	normal;
	float	damagetime;

	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner && self->owner->client)
		PlayerNoise (self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (plane)
			VectorCopy (plane->normal, normal);
		else
			VectorClear (normal);

		if ((other->svflags & SVF_MONSTER) || other->client)
		{
			if (other->health > 0)
			{
				// knockback only for the living, and the burn
				T_Damage (other, self, self->owner, self->velocity, self->s.origin,
					normal, 0, self->dmg * 3, TRACKER_IMPACT_FLAGS, MOD_TRACKER);
				if (!(other->flags & (FL_FLY|FL_SWIM)))
					other->velocity[2] += 140;

				damagetime = ((float)self->dmg) * FRAMETIME / TRACKER_DAMAGE_TIME;
				tracker_pain_daemon_spawn (self->owner, other, (int)damagetime);
			}
			else	// all but a gib for the dead
				T_Damage (other, self, self->owner, self->velocity, self->s.origin, normal,
					self->dmg * 4, self->dmg * 3, TRACKER_IMPACT_FLAGS, MOD_TRACKER);
		}
		else	// full damage at once for the inanimate
			T_Damage (other, self, self->owner, self->velocity, self->s.origin, normal,
				self->dmg, self->dmg * 3, TRACKER_IMPACT_FLAGS, MOD_TRACKER);
	}

	tracker_explode (self);
}

static void tracker_fly (edict_t *self)
{
	vec3_t	dest, dir, center;

	if (!self->enemy || !self->enemy->inuse || self->enemy->health < 1)
	{
		tracker_explode (self);
		return;
	}

	// for its enemy's middle, or a player's eyes
	if (self->enemy->client)
	{
		VectorCopy (self->enemy->s.origin, dest);
		dest[2] += self->enemy->viewheight;
	}
	else if (VectorCompare (self->enemy->absmin, vec3_origin) || VectorCompare (self->enemy->absmax, vec3_origin))
		VectorCopy (self->enemy->s.origin, dest);
	else
	{
		VectorMA (vec3_origin, 0.5, self->enemy->absmin, center);
		VectorMA (center, 0.5, self->enemy->absmax, center);
		VectorCopy (center, dest);
	}

	VectorSubtract (dest, self->s.origin, dir);
	VectorNormalize (dir);
	vectoangles2 (dir, self->s.angles);
	VectorScale (dir, self->speed, self->velocity);
	VectorCopy (dest, self->monsterinfo.saved_goal);

	self->nextthink = level.time + 0.1;
}

void fire_tracker (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, edict_t *enemy)
{
	edict_t	*bolt;
	trace_t	tr;

	VectorNormalize (dir);

	bolt = G_Spawn ();
	VectorCopy (start, bolt->s.origin);
	VectorCopy (start, bolt->s.old_origin);
	vectoangles2 (dir, bolt->s.angles);
	VectorScale (dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->speed = speed;
	bolt->s.effects = EF_TRACKER;
	bolt->s.sound = gi.soundindex ("weapons/disrupt.wav");
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->s.modelindex = gi.modelindex ("models/proj/disintegrator/tris.md2");
	bolt->touch = tracker_touch;
	bolt->enemy = enemy;
	bolt->owner = self;
	bolt->dmg = damage;
	bolt->classname = "tracker";
	gi.linkentity (bolt);

	if (enemy)
	{
		bolt->nextthink = level.time + 0.1;
		bolt->think = tracker_fly;
	}
	else
	{
		bolt->nextthink = level.time + 10;
		bolt->think = G_FreeEdict;
	}

	if (self->client)
		check_dodge (self, bolt->s.origin, dir, speed);

	tr = gi.trace (self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA (bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch (bolt, tr.ent, NULL, NULL);
	}
}

/*
 * Ground Zero's heat beam (its plasma beam): an instant beam, halved
 * through water
 */
static void fire_beams (edict_t *self, vec3_t start, vec3_t aimdir, vec3_t offset, int damage, int kick, int te_beam, int te_impact, int mod)
{
	trace_t	tr;
	vec3_t	dir, forward, right, up, end, water_start, endpoint, beam_endpt, pos;
	qboolean	water = false, underwater = false;
	int		content_mask = MASK_SHOT | MASK_WATER;

	vectoangles2 (aimdir, dir);
	AngleVectors (dir, forward, right, up);
	VectorMA (start, 8192, forward, end);

	if (gi.pointcontents (start) & MASK_WATER)
	{
		underwater = true;
		VectorCopy (start, water_start);
		content_mask &= ~MASK_WATER;
	}

	tr = gi.trace (start, NULL, NULL, end, self, content_mask);

	// into water?
	if (tr.contents & MASK_WATER)
	{
		water = true;
		VectorCopy (tr.endpos, water_start);

		if (!VectorCompare (start, tr.endpos))
		{
			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_HEATBEAM_SPARKS);
			gi.WritePosition (water_start);
			gi.WriteDir (tr.plane.normal);
			gi.multicast (tr.endpos, MULTICAST_PVS);
		}

		// and through it
		tr = gi.trace (water_start, NULL, NULL, end, self, MASK_SHOT);
	}

	VectorCopy (tr.endpos, endpoint);

	// half the damage at a target in water
	if (water)
		damage = damage / 2;

	// the puff, or the damage
	if (!(tr.surface && (tr.surface->flags & SURF_SKY)) && tr.fraction < 1.0)
	{
		if (tr.ent->takedamage)
			T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_ENERGY, mod);
		else if (!water && tr.surface && strncmp (tr.surface->name, "sky", 3))
		{
			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_HEATBEAM_STEAM);
			gi.WritePosition (tr.endpos);
			gi.WriteDir (tr.plane.normal);
			gi.multicast (tr.endpos, MULTICAST_PVS);

			if (self->client)
				PlayerNoise (self, tr.endpos, PNOISE_IMPACT);
		}
	}

	// through water: where it ends, and a bubble trail
	if (water || underwater)
	{
		VectorSubtract (tr.endpos, water_start, dir);
		VectorNormalize (dir);
		VectorMA (tr.endpos, -2, dir, pos);
		if (gi.pointcontents (pos) & MASK_WATER)
			VectorCopy (pos, tr.endpos);
		else
			tr = gi.trace (pos, NULL, NULL, water_start, tr.ent, MASK_WATER);

		VectorAdd (water_start, tr.endpos, pos);
		VectorScale (pos, 0.5, pos);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BUBBLETRAIL2);
		gi.WritePosition (water_start);
		gi.WritePosition (tr.endpos);
		gi.multicast (pos, MULTICAST_PVS);
	}

	if (!underwater && !water)
		VectorCopy (tr.endpos, beam_endpt);
	else
		VectorCopy (endpoint, beam_endpt);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (te_beam);
	gi.WriteShort (self - g_edicts);
	gi.WritePosition (start);
	gi.WritePosition (beam_endpt);
	gi.multicast (self->s.origin, MULTICAST_ALL);
}

void fire_heatbeam (edict_t *self, vec3_t start, vec3_t aimdir, vec3_t offset, int damage, int kick, qboolean monster)
{
	fire_beams (self, start, aimdir, offset, damage, kick,
		monster ? TE_MONSTER_HEATBEAM : TE_HEATBEAM, TE_HEATBEAM_SPARKS, MOD_HEATBEAM);
}

/*
 * Ground Zero's bad areas: a tesla's field, marked once a monster is
 * zapped, which monsters attack the tesla from and keep out of
 */
static void badarea_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
}

static edict_t *SpawnBadArea (vec3_t mins, vec3_t maxs, float lifespan, edict_t *owner)
{
	edict_t	*badarea;
	vec3_t	origin, bmins, bmaxs;

	VectorAdd (mins, maxs, origin);
	VectorScale (origin, 0.5, origin);
	VectorSubtract (maxs, origin, bmaxs);
	VectorSubtract (mins, origin, bmins);

	badarea = G_Spawn ();
	VectorCopy (origin, badarea->s.origin);
	VectorCopy (bmaxs, badarea->maxs);
	VectorCopy (bmins, badarea->mins);
	badarea->touch = badarea_touch;
	badarea->movetype = MOVETYPE_NONE;
	badarea->solid = SOLID_TRIGGER;
	badarea->classname = "bad_area";
	gi.linkentity (badarea);

	if (lifespan)
	{
		badarea->think = G_FreeEdict;
		badarea->nextthink = level.time + lifespan;
	}
	badarea->owner = owner;
	return badarea;
}

// the bad area it's in, if any
edict_t *CheckForBadArea (edict_t *ent)
{
	edict_t	*touch[MAX_EDICTS], *hit;
	vec3_t	mins, maxs;
	int		i, num;

	VectorAdd (ent->s.origin, ent->mins, mins);
	VectorAdd (ent->s.origin, ent->maxs, maxs);

	num = gi.BoxEdicts (mins, maxs, touch, MAX_EDICTS, AREA_TRIGGERS);
	// (one may be removed before it's reached: killtargeted)
	for (i = 0; i < num; i++)
	{
		hit = touch[i];
		if (hit->inuse && hit->touch == badarea_touch)
			return hit;
	}
	return NULL;
}

#define	TESLA_DAMAGE_RADIUS		128

// mark a tesla's field bad, if it isn't already; true if it now is
qboolean MarkTeslaArea (edict_t *self, edict_t *tesla)
{
	vec3_t	mins, maxs;
	edict_t	*e, *tail, *area;

	// not twice
	tail = tesla;
	for (e = tesla->teamchain; e; e = e->teamchain)
	{
		tail = e;
		if (!strcmp (e->classname, "bad_area"))
			return false;
	}

	if (tesla->teamchain && tesla->teamchain->inuse)
	{
		// its trigger's
		VectorCopy (tesla->teamchain->absmin, mins);
		VectorCopy (tesla->teamchain->absmax, maxs);
		area = SpawnBadArea (mins, maxs, tesla->air_finished ? tesla->air_finished - level.time : tesla->nextthink - level.time, tesla);
	}
	else
	{
		// a guess at its field, and how long it lasts
		VectorSet (mins, -TESLA_DAMAGE_RADIUS, -TESLA_DAMAGE_RADIUS, tesla->mins[2]);
		VectorSet (maxs, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS);
		VectorAdd (mins, tesla->s.origin, mins);
		VectorAdd (maxs, tesla->s.origin, maxs);
		area = SpawnBadArea (mins, maxs, 30, tesla);
	}

	if (area)
		tail->teamchain = area;
	return true;
}

// a monster goes for the tesla, remembering who it was fighting
void TargetTesla (edict_t *self, edict_t *tesla)
{
	// a medic gives up healing
	if (self->monsterinfo.aiflags & AI_MEDIC)
		self->monsterinfo.aiflags &= ~AI_MEDIC;

	if (self->enemy != tesla)
	{
		self->oldenemy = self->enemy;
		self->enemy = tesla;
		if (self->monsterinfo.attack)
		{
			if (self->health <= 0)
				return;
			self->monsterinfo.attack (self);
		}
		else
			FoundTarget (self);
	}
}

/*
 * Ground Zero's tesla mine: thrown, it opens, and zaps monsters within its
 * field with lightning for 30 seconds; lava or slime, or its field's end,
 * blows it up
 */
#define	TESLA_TIME_TO_LIVE			30
#define	TESLA_DAMAGE				3
#define	TESLA_KNOCKBACK				8
#define	TESLA_ACTIVATE_TIME			3
#define	TESLA_EXPLOSION_DAMAGE_MULT	50
#define	TESLA_EXPLOSION_RADIUS		200

void Grenade_Explode (edict_t *ent);

static void tesla_remove (edict_t *self)
{
	edict_t	*cur, *next;

	self->takedamage = DAMAGE_NO;

	// its field, and any bad area
	for (cur = self->teamchain; cur; cur = next)
	{
		next = cur->teamchain;
		G_FreeEdict (cur);
	}
	self->teamchain = NULL;

	self->owner = self->teammaster;		// going away: its owner right
	self->enemy = NULL;

	// a double or quad sound for a boosted explosion
	if (self->dmg_radius)
	{
		if (self->dmg >= TESLA_DAMAGE * TESLA_EXPLOSION_DAMAGE_MULT * 4)
			gi.sound (self, CHAN_ITEM, gi.soundindex ("items/damage3.wav"), 1, ATTN_NORM, 0);
		else if (self->dmg == TESLA_DAMAGE * TESLA_EXPLOSION_DAMAGE_MULT * 2)
			gi.sound (self, CHAN_ITEM, gi.soundindex ("misc/ddamage3.wav"), 1, ATTN_NORM, 0);
	}

	Grenade_Explode (self);
}

static void tesla_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	tesla_remove (self);
}

static void tesla_blow (edict_t *self)
{
	self->dmg = self->dmg * TESLA_EXPLOSION_DAMAGE_MULT;
	self->dmg_radius = TESLA_EXPLOSION_RADIUS;
	tesla_remove (self);
}

static void tesla_zap (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
}

static void tesla_think_active (edict_t *self)
{
	edict_t	*touch[MAX_EDICTS], *hit;
	vec3_t	dir, start;
	trace_t	tr;
	int		i, num;

	if (level.time > self->air_finished)
	{
		tesla_remove (self);
		return;
	}

	VectorCopy (self->s.origin, start);
	start[2] += 16;

	num = gi.BoxEdicts (self->teamchain->absmin, self->teamchain->absmax, touch, MAX_EDICTS, AREA_SOLID);
	for (i = 0; i < num; i++)
	{
		// it may have died zapping
		if (!self->inuse)
			break;

		hit = touch[i];
		if (!hit->inuse || hit == self || hit->health < 1)
			continue;
		// no players in single player or coop
		if (hit->client && (coop->value || !deathmatch->value))
			continue;
		if (!(hit->svflags & SVF_MONSTER) && !hit->client)
			continue;

		tr = gi.trace (start, vec3_origin, vec3_origin, hit->s.origin, self, MASK_SHOT);
		if (tr.fraction == 1 || tr.ent == hit)
		{
			VectorSubtract (hit->s.origin, start, dir);

			// a double or quad sound, above the "normal" damage
			if (self->dmg >= TESLA_DAMAGE * 4)
				gi.sound (self, CHAN_ITEM, gi.soundindex ("items/damage3.wav"), 1, ATTN_NORM, 0);
			else if (self->dmg == TESLA_DAMAGE * 2)
				gi.sound (self, CHAN_ITEM, gi.soundindex ("misc/ddamage3.wav"), 1, ATTN_NORM, 0);

			// no knockback for walking monsters
			if ((hit->svflags & SVF_MONSTER) && !(hit->flags & (FL_FLY|FL_SWIM)))
				T_Damage (hit, self, self->teammaster, dir, tr.endpos, tr.plane.normal, self->dmg, 0, 0, MOD_TESLA_MINE);
			else
				T_Damage (hit, self, self->teammaster, dir, tr.endpos, tr.plane.normal, self->dmg, TESLA_KNOCKBACK, 0, MOD_TESLA_MINE);

			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_LIGHTNING);
			gi.WriteShort (hit - g_edicts);		// destination entity
			gi.WriteShort (self - g_edicts);	// source entity
			gi.WritePosition (tr.endpos);
			gi.WritePosition (start);
			gi.multicast (start, MULTICAST_PVS);
		}
	}

	if (self->inuse)
	{
		self->think = tesla_think_active;
		self->nextthink = level.time + FRAMETIME;
	}
}

static void tesla_activate (edict_t *self)
{
	edict_t	*trigger, *search;

	if (gi.pointcontents (self->s.origin) & (CONTENTS_SLIME|CONTENTS_LAVA|CONTENTS_WATER))
	{
		tesla_blow (self);
		return;
	}

	// not by a spawn point, in deathmatch
	if (deathmatch->value)
	{
		search = NULL;
		while ((search = findradius (search, self->s.origin, 1.5 * TESLA_DAMAGE_RADIUS)) != NULL)
		{
			if (search->classname
				&& (!strcmp (search->classname, "info_player_deathmatch")
				|| !strcmp (search->classname, "info_player_start")
				|| !strcmp (search->classname, "info_player_coop")
				|| !strcmp (search->classname, "misc_teleporter_dest"))
				&& visible (search, self))
			{
				tesla_remove (self);
				return;
			}
		}
	}

	trigger = G_Spawn ();
	VectorCopy (self->s.origin, trigger->s.origin);
	VectorSet (trigger->mins, -TESLA_DAMAGE_RADIUS, -TESLA_DAMAGE_RADIUS, self->mins[2]);
	VectorSet (trigger->maxs, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS);
	trigger->movetype = MOVETYPE_NONE;
	trigger->solid = SOLID_TRIGGER;
	trigger->owner = self;
	trigger->touch = tesla_zap;
	trigger->classname = "tesla trigger";
	// (no need for a teamslave: bouncing moves its teamchain)
	gi.linkentity (trigger);

	VectorClear (self->s.angles);

	// no owner in deathmatch
	if (deathmatch->value)
		self->owner = NULL;

	self->teamchain = trigger;
	self->think = tesla_think_active;
	self->nextthink = level.time + FRAMETIME;
	self->air_finished = level.time + TESLA_TIME_TO_LIVE;
}

static void tesla_think (edict_t *ent)
{
	if (gi.pointcontents (ent->s.origin) & (CONTENTS_SLIME|CONTENTS_LAVA))
	{
		tesla_remove (ent);
		return;
	}

	VectorClear (ent->s.angles);

	if (!ent->s.frame)
		gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/teslaopen.wav"), 1, ATTN_NORM, 0);

	ent->s.frame++;
	if (ent->s.frame > 14)
	{
		ent->s.frame = 14;
		ent->think = tesla_activate;
		ent->nextthink = level.time + 0.1;
		return;
	}

	// it opens, its skin warming
	if (ent->s.frame == 10)
	{
		if (ent->owner && ent->owner->client)
			PlayerNoise (ent->owner, ent->s.origin, PNOISE_WEAPON);
		ent->s.skinnum = 1;
	}
	else if (ent->s.frame == 12)
		ent->s.skinnum = 2;
	else if (ent->s.frame == 14)
		ent->s.skinnum = 3;

	ent->think = tesla_think;
	ent->nextthink = level.time + 0.1;
}

static void tesla_lava (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	land_point, normal;

	if (plane)
		VectorCopy (plane->normal, normal);
	else
		VectorClear (normal);

	VectorMA (ent->s.origin, -20.0, normal, land_point);
	if (gi.pointcontents (land_point) & (CONTENTS_SLIME|CONTENTS_LAVA))
	{
		tesla_blow (ent);
		return;
	}

	if (random() > 0.5)
		gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
	else
		gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
}

void fire_tesla (edict_t *self, vec3_t start, vec3_t aimdir, int damage_multiplier, int speed)
{
	edict_t	*tesla;
	vec3_t	dir, forward, right, up;

	vectoangles2 (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	tesla = G_Spawn ();
	VectorCopy (start, tesla->s.origin);
	VectorScale (aimdir, speed, tesla->velocity);
	VectorMA (tesla->velocity, 200 + crandom() * 10.0, up, tesla->velocity);
	VectorMA (tesla->velocity, crandom() * 10.0, right, tesla->velocity);
	VectorClear (tesla->s.angles);
	tesla->movetype = MOVETYPE_BOUNCE;
	tesla->solid = SOLID_BBOX;
	tesla->s.effects |= EF_GRENADE;
	tesla->s.renderfx |= RF_IR_VISIBLE;
	VectorSet (tesla->mins, -12, -12, 0);
	VectorSet (tesla->maxs, 12, 12, 20);
	tesla->s.modelindex = gi.modelindex ("models/weapons/g_tesla/tris.md2");

	tesla->owner = self;
	tesla->teammaster = self;

	tesla->wait = level.time + TESLA_TIME_TO_LIVE;
	tesla->think = tesla_think;
	tesla->nextthink = level.time + TESLA_ACTIVATE_TIME;

	// blow up on lava and slime
	tesla->touch = tesla_lava;

	tesla->health = deathmatch->value ? 20 : 30;
	tesla->takedamage = DAMAGE_YES;
	tesla->die = tesla_die;
	tesla->dmg = TESLA_DAMAGE * damage_multiplier;
	tesla->classname = "tesla";
	tesla->clipmask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;

	gi.linkentity (tesla);
}

/*
==============================================================================

Ground Zero's prox mines and A-M bomb, as the re-release has them

==============================================================================
*/

#define PROX_TIME_TO_LIVE	45		// 45, 30, 15, 10
#define PROX_TIME_DELAY		0.5
#define PROX_BOUND_SIZE		96
#define PROX_DAMAGE_RADIUS	192
#define PROX_HEALTH			20
#define PROX_DAMAGE			90

static void Prox_Explode (edict_t *ent)
{
	vec3_t	origin;
	edict_t	*owner;

	// free the trigger field
	if (ent->teamchain && ent->teamchain->owner == ent)
		G_FreeEdict (ent->teamchain);

	owner = ent;
	if (ent->teammaster)
	{
		owner = ent->teammaster;
		PlayerNoise (owner, ent->s.origin, PNOISE_IMPACT);
	}

	// play quad sound if appropriate
	if (ent->dmg > PROX_DAMAGE)
		gi.sound (ent, CHAN_ITEM, gi.soundindex ("items/damage3.wav"), 1, ATTN_NORM, 0);

	ent->takedamage = DAMAGE_NO;
	T_RadiusDamage (ent, owner, ent->dmg, ent, PROX_DAMAGE_RADIUS, MOD_PROX);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	gi.WriteByte (svc_temp_entity);
	if (ent->groundentity)
		gi.WriteByte (TE_GRENADE_EXPLOSION);
	else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}

static void prox_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	// if set off by another prox, delay a little (chained explosions)
	self->takedamage = DAMAGE_NO;
	if (!inflictor->classname || strcmp (inflictor->classname, "prox_mine"))
		Prox_Explode (self);
	else
	{
		self->think = Prox_Explode;
		self->nextthink = level.time + FRAMETIME;
	}
}

static void Prox_Field_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	edict_t *prox;

	if (!(other->svflags & SVF_MONSTER) && !other->client)
		return;

	// trigger the prox mine if it's still there, and still mine.
	prox = ent->owner;

	// players only in deathmatch, and not its owner
	if (other->client && (!deathmatch->value || other == prox->teammaster))
		return;

	if (other == prox) // don't set self off
		return;

	if (prox->think == Prox_Explode) // we're set to blow!
		return;

	if (prox->teamchain == ent)
	{
		gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/proxwarn.wav"), 1, ATTN_NORM, 0);
		prox->think = Prox_Explode;
		prox->nextthink = level.time + PROX_TIME_DELAY;
		return;
	}

	ent->solid = SOLID_NOT;
	G_FreeEdict (ent);
}

static void prox_seek (edict_t *ent)
{
	if (level.time > ent->wait)
		Prox_Explode (ent);
	else
	{
		ent->s.frame++;
		if (ent->s.frame > 13)
			ent->s.frame = 9;
		ent->think = prox_seek;
		ent->nextthink = level.time + FRAMETIME;
	}
}

static void prox_open (edict_t *ent)
{
	edict_t *search = NULL;

	if (ent->s.frame == 9) // end of opening animation
	{
		// set the owner to NULL so the owner can walk through it.  needs to be done here so the owner
		// doesn't get stuck on it while it's opening if fired at point blank wall
		ent->s.sound = 0;

		if (deathmatch->value)
			ent->owner = NULL;

		if (ent->teamchain)
			ent->teamchain->touch = Prox_Field_Touch;
		while ((search = findradius (search, ent->s.origin, PROX_DAMAGE_RADIUS + 10)) != NULL)
		{
			if (!search->classname) // tag token and other weird shit
				continue;
			if (search == ent)
				continue;

			// if it's a monster or player with health > 0
			// or it's a player start point
			// and we can see it
			// blow up
			if ((((search->svflags & SVF_MONSTER) || (deathmatch->value && (search->client || !strcmp (search->classname, "prox_mine")))) && (search->health > 0)) ||
				(deathmatch->value &&
				 (!strncmp (search->classname, "info_player_", 12) ||
				  !strcmp (search->classname, "misc_teleporter_dest") ||
				  !strncmp (search->classname, "item_flag_", 10))))
			{
				if (search->client && search == ent->teammaster)
					continue;
				if (!visible (search, ent))
					continue;
				gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/proxwarn.wav"), 1, ATTN_NORM, 0);
				Prox_Explode (ent);
				return;
			}
		}

		switch (ent->dmg / PROX_DAMAGE)
		{
		case 2:
			ent->wait = level.time + 30;
			break;
		case 4:
			ent->wait = level.time + 15;
			break;
		case 8:
			ent->wait = level.time + 10;
			break;
		default:
			ent->wait = level.time + PROX_TIME_TO_LIVE;
			break;
		}

		ent->think = prox_seek;
		ent->nextthink = level.time + 0.2;
	}
	else
	{
		if (ent->s.frame == 0)
			gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/proxopen.wav"), 1, ATTN_NORM, 0);
		ent->s.frame++;
		ent->think = prox_open;
		ent->nextthink = level.time + FRAMETIME;
	}
}

#define PROX_STOP_EPSILON	0.1

static void prox_land (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	edict_t	*field;
	vec3_t	dir, forward, right, up, land_point;
	int		movetype = MOVETYPE_NONE;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (plane)
	{
		VectorMA (ent->s.origin, -10.0f, plane->normal, land_point);
		if (gi.pointcontents (land_point) & (CONTENTS_SLIME | CONTENTS_LAVA))
		{
			Prox_Explode (ent);
			return;
		}
	}

	// a monster, a player, or a mine, a trap or a bomb: blow up on it
	if (!plane || (other->svflags & SVF_MONSTER) || other->client || (other != world && other->takedamage && other->solid == SOLID_BBOX))
	{
		if (other != ent->teammaster)
			Prox_Explode (ent);
		return;
	}
	else if (other != world)
	{
		// Here we need to check to see if we can stop on this entity.
		vec3_t	out;
		float	backoff, change;
		int		i, stick_ok;

		if ((other->movetype == MOVETYPE_PUSH) && (plane->normal[2] > 0.7))
			stick_ok = 1;
		else
			stick_ok = 0;

		backoff = DotProduct (ent->velocity, plane->normal) * 1.5f;
		for (i = 0; i < 3; i++)
		{
			change = plane->normal[i] * backoff;
			out[i] = ent->velocity[i] - change;
			if (out[i] > -PROX_STOP_EPSILON && out[i] < PROX_STOP_EPSILON)
				out[i] = 0;
		}

		if (out[2] > 60)
			return;

		movetype = MOVETYPE_BOUNCE;

		// if we're here, we're going to stop on an entity
		if (stick_ok)
		{ // it's a happy entity
			VectorClear (ent->velocity);
			VectorClear (ent->avelocity);
		}
		else // no-stick.  teflon time
		{
			if (plane->normal[2] > 0.7)
			{
				Prox_Explode (ent);
				return;
			}
			return;
		}
	}
	else if (other->s.modelindex != 1)
		return;

	vectoangles2 (plane->normal, dir);
	AngleVectors (dir, forward, right, up);

	if (gi.pointcontents (ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME))
	{
		Prox_Explode (ent);
		return;
	}

	field = G_Spawn ();

	VectorCopy (ent->s.origin, field->s.origin);
	VectorSet (field->mins, -PROX_BOUND_SIZE, -PROX_BOUND_SIZE, -PROX_BOUND_SIZE);
	VectorSet (field->maxs, PROX_BOUND_SIZE, PROX_BOUND_SIZE, PROX_BOUND_SIZE);
	field->movetype = MOVETYPE_NONE;
	field->solid = SOLID_TRIGGER;
	field->owner = ent;
	field->classname = "prox_field";
	field->teammaster = ent;
	gi.linkentity (field);

	VectorClear (ent->velocity);
	VectorClear (ent->avelocity);
	// rotate to vertical
	dir[PITCH] = dir[PITCH] + 90;
	VectorCopy (dir, ent->s.angles);
	ent->takedamage = DAMAGE_AIM;
	ent->movetype = movetype; // either bounce or none, depending on whether we stuck to something
	ent->die = prox_die;
	ent->teamchain = field;
	ent->health = PROX_HEALTH;
	ent->nextthink = level.time;
	ent->think = prox_open;
	ent->touch = NULL;
	ent->solid = SOLID_BBOX;

	gi.linkentity (ent);
}

// in flight: pointed the way it goes, till its time runs out
static void Prox_Think (edict_t *self)
{
	vec3_t	dir;

	if (self->timestamp <= level.time)
	{
		Prox_Explode (self);
		return;
	}

	VectorCopy (self->velocity, dir);
	VectorNormalize (dir);
	vectoangles2 (dir, self->s.angles);
	self->s.angles[PITCH] -= 90;
	self->nextthink = level.time + FRAMETIME;
}

void fire_prox (edict_t *self, vec3_t start, vec3_t aimdir, int prox_damage_multiplier, int speed)
{
	edict_t	*prox;
	vec3_t	dir;
	vec3_t	forward, right, up;
	float	gravityAdjustment = sv_gravity->value / 800.f;

	vectoangles2 (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	prox = G_Spawn ();
	VectorCopy (start, prox->s.origin);
	VectorScale (aimdir, speed, prox->velocity);
	VectorMA (prox->velocity, (200 + crandom () * 10.0f) * gravityAdjustment, up, prox->velocity);
	VectorMA (prox->velocity, crandom () * 10.0f, right, prox->velocity);
	VectorCopy (dir, prox->s.angles);
	prox->s.angles[PITCH] -= 90;
	prox->movetype = MOVETYPE_BOUNCE;
	prox->solid = SOLID_BBOX;
	prox->s.effects |= EF_GRENADE;
	prox->clipmask = MASK_SHOT | CONTENTS_LAVA | CONTENTS_SLIME;
	prox->s.renderfx |= RF_IR_VISIBLE;
	VectorSet (prox->mins, -6, -6, -6);
	VectorSet (prox->maxs, 6, 6, 6);
	prox->s.modelindex = gi.modelindex ("models/weapons/g_prox/tris.md2");
	prox->owner = self;
	prox->teammaster = self;
	prox->touch = prox_land;
	prox->think = Prox_Think;
	prox->nextthink = level.time + FRAMETIME;
	prox->dmg = PROX_DAMAGE * prox_damage_multiplier;
	prox->classname = "prox_mine";

	switch (prox_damage_multiplier)
	{
	case 2:
		prox->timestamp = level.time + 30;
		break;
	case 4:
		prox->timestamp = level.time + 15;
		break;
	case 8:
		prox->timestamp = level.time + 10;
		break;
	default:
		prox->timestamp = level.time + PROX_TIME_TO_LIVE;
		break;
	}

	gi.linkentity (prox);
}

#define NUKE_DELAY			4
#define NUKE_TIME_TO_LIVE	6
#define NUKE_RADIUS			512
#define NUKE_DAMAGE			400
#define NUKE_QUAKE_TIME		3
#define NUKE_QUAKE_STRENGTH	100

/*
Like T_RadiusDamage, but through walls: within the radius, 10000;
beyond it, falling off to none at twice it. Players who see it go off
are blinded for a while (client->nuke_framenum).
*/
void T_RadiusNukeDamage (edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod)
{
	float	points, len, killzone, killzone2, dist;
	edict_t	*ent = NULL;
	vec3_t	v, dir;
	trace_t	tr;
	int		i;

	killzone = radius;
	killzone2 = radius * 2.0f;

	while ((ent = findradius (ent, inflictor->s.origin, killzone2)) != NULL)
	{
		// ignore nobody
		if (ent == ignore)
			continue;
		if (!ent->takedamage)
			continue;
		if (!ent->inuse)
			continue;
		if (!(ent->client || (ent->svflags & SVF_MONSTER)))
			continue;

		VectorAdd (ent->mins, ent->maxs, v);
		VectorMA (ent->s.origin, 0.5, v, v);
		VectorSubtract (inflictor->s.origin, v, v);
		len = VectorLength (v);
		if (len <= killzone)
			points = 10000;
		else if (len <= killzone2)
			points = (damage / killzone) * (killzone2 - len);
		else
			points = 0;

		if (points > 0)
		{
			if (ent->client)
				ent->client->nuke_framenum = level.framenum + 20;
			VectorSubtract (ent->s.origin, inflictor->s.origin, dir);
			T_Damage (ent, inflictor, attacker, dir, inflictor->s.origin, vec3_origin, (int) points, (int) points, DAMAGE_RADIUS, mod);
		}
	}

	// the flash, for every player: blinding in sight, less behind a wall
	for (i = 1; i <= game.maxclients; i++)
	{
		ent = g_edicts + i;
		if (!ent->inuse || !ent->client || ent->client->nuke_framenum == level.framenum + 20)
			continue;
		tr = gi.trace (inflictor->s.origin, NULL, NULL, ent->s.origin, inflictor, MASK_SOLID);
		if (tr.fraction == 1.0f)
			ent->client->nuke_framenum = level.framenum + 20;
		else
		{
			dist = realrange (ent, inflictor);
			if (dist < 2048)
			{
				if (ent->client->nuke_framenum < level.framenum + 15)
					ent->client->nuke_framenum = level.framenum + 15;
			}
			else if (ent->client->nuke_framenum < level.framenum + 10)
				ent->client->nuke_framenum = level.framenum + 10;
		}
	}
}

static void Nuke_Quake (edict_t *self)
{
	int		i;
	edict_t	*e;

	if (self->last_move_time < level.time)
	{
		gi.positioned_sound (self->s.origin, self, CHAN_AUTO, self->noise_index, 0.75, ATTN_NONE, 0);
		self->last_move_time = level.time + 0.5;
	}

	for (i = 1, e = g_edicts + i; i < globals.num_edicts; i++, e++)
	{
		if (!e->inuse)
			continue;
		if (!e->client)
			continue;
		if (!e->groundentity)
			continue;

		e->groundentity = NULL;
		e->velocity[0] += crandom () * 150;
		e->velocity[1] += crandom () * 150;
		e->velocity[2] = self->speed * (100.0f / e->mass);
	}

	if (level.time < self->timestamp)
		self->nextthink = level.time + FRAMETIME;
	else
		G_FreeEdict (self);
}

static void Nuke_Explode (edict_t *ent)
{
	if (ent->teammaster->client)
		PlayerNoise (ent->teammaster, ent->s.origin, PNOISE_IMPACT);

	T_RadiusNukeDamage (ent, ent->teammaster, (float) ent->dmg, ent, ent->dmg_radius, MOD_NUKE);

	if (ent->dmg > NUKE_DAMAGE)
		gi.sound (ent, CHAN_ITEM, gi.soundindex ("items/damage3.wav"), 1, ATTN_NORM, 0);

	gi.sound (ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex ("weapons/grenlx1a.wav"), 1, ATTN_NONE, 0);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1_BIG);
	gi.WritePosition (ent->s.origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_NUKEBLAST);
	gi.WritePosition (ent->s.origin);
	gi.multicast (ent->s.origin, MULTICAST_ALL);

	// become a quake
	ent->svflags |= SVF_NOCLIENT;
	ent->noise_index = gi.soundindex ("world/rumble.wav");
	ent->think = Nuke_Quake;
	ent->speed = NUKE_QUAKE_STRENGTH;
	ent->timestamp = level.time + NUKE_QUAKE_TIME;
	ent->nextthink = level.time + FRAMETIME;
	ent->last_move_time = 0;
}

static void nuke_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	self->takedamage = DAMAGE_NO;
	if (attacker && attacker->classname && !strcmp (attacker->classname, "nuke"))
	{
		G_FreeEdict (self);
		return;
	}
	Nuke_Explode (self);
}

static void Nuke_Think (edict_t *ent)
{
	float	attenuation, default_atten = 1.8f;
	int		muzzleflash;

	switch (ent->dmg / NUKE_DAMAGE)
	{
	case 2:
		attenuation = default_atten / 2.0f;
		muzzleflash = MZ_NUKE2;
		break;
	case 4:
		attenuation = default_atten / 3.0f;
		muzzleflash = MZ_NUKE4;
		break;
	case 8:
		attenuation = default_atten / 5.0f;
		muzzleflash = MZ_NUKE8;
		break;
	default:
		attenuation = default_atten / 1.4f;
		muzzleflash = MZ_NUKE1;
		break;
	}

	if (ent->wait < level.time)
		Nuke_Explode (ent);
	else if (level.time >= (ent->wait - NUKE_TIME_TO_LIVE))
	{
		ent->s.frame++;

		if (ent->s.frame > 11)
			ent->s.frame = 6;

		if (gi.pointcontents (ent->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA))
		{
			Nuke_Explode (ent);
			return;
		}

		ent->think = Nuke_Think;
		ent->nextthink = level.time + FRAMETIME;
		ent->health = 1;
		ent->owner = NULL;

		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (ent - g_edicts);
		gi.WriteByte (muzzleflash);
		gi.multicast (ent->s.origin, MULTICAST_PHS);

		if (ent->timestamp <= level.time)
		{
			gi.sound (ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex ("weapons/nukewarn2.wav"), 1, attenuation, 0);
			if ((ent->wait - level.time) <= (NUKE_TIME_TO_LIVE / 2.0f))
				ent->timestamp = level.time + 0.3;
			else
				ent->timestamp = level.time + 0.5;
		}
	}
	else
	{
		if (ent->timestamp <= level.time)
		{
			gi.sound (ent, CHAN_NO_PHS_ADD | CHAN_VOICE, gi.soundindex ("weapons/nukewarn2.wav"), 1, attenuation, 0);
			ent->timestamp = level.time + 1;
		}
		ent->nextthink = level.time + FRAMETIME;
	}
}

static void nuke_bounce (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (surf)
	{
		if (random () > 0.5)
			gi.sound (ent, CHAN_BODY, gi.soundindex ("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
		else
			gi.sound (ent, CHAN_BODY, gi.soundindex ("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
	}
}

void fire_nuke (edict_t *self, vec3_t start, vec3_t aimdir, int speed, int damage_modifier)
{
	edict_t	*nuke;
	vec3_t	dir;
	vec3_t	forward, right, up;

	vectoangles2 (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	nuke = G_Spawn ();
	VectorCopy (start, nuke->s.origin);
	VectorScale (aimdir, speed, nuke->velocity);
	VectorMA (nuke->velocity, 200 + crandom () * 10.0f, up, nuke->velocity);
	VectorMA (nuke->velocity, crandom () * 10.0f, right, nuke->velocity);
	nuke->movetype = MOVETYPE_BOUNCE;
	nuke->clipmask = MASK_SHOT;
	nuke->solid = SOLID_BBOX;
	nuke->s.effects |= EF_GRENADE;
	nuke->s.renderfx |= RF_IR_VISIBLE;
	VectorSet (nuke->mins, -8, -8, 0);
	VectorSet (nuke->maxs, 8, 8, 16);
	nuke->s.modelindex = gi.modelindex ("models/weapons/g_nuke/tris.md2");
	nuke->owner = self;
	nuke->teammaster = self;
	nuke->nextthink = level.time + FRAMETIME;
	nuke->wait = level.time + NUKE_DELAY + NUKE_TIME_TO_LIVE;
	nuke->think = Nuke_Think;
	nuke->touch = nuke_bounce;

	nuke->health = 10000;
	nuke->takedamage = DAMAGE_YES;
	nuke->dmg = NUKE_DAMAGE * damage_modifier;
	if (damage_modifier == 1)
		nuke->dmg_radius = NUKE_RADIUS;
	else
		nuke->dmg_radius = NUKE_RADIUS + NUKE_RADIUS * (0.25f * (float) damage_modifier);
	// this yields 1.0, 1.5, 2.0, 3.0 times radius

	nuke->classname = "nuke";
	nuke->die = nuke_die;

	gi.linkentity (nuke);
}
