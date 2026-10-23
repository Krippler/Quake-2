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
