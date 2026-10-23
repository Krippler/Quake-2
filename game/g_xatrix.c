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
g_xatrix.c -- The Reckoning's weapons that its monsters fire, from its game
code (xatrix/src, Yamagi's), for the monsters of it that Call of the
Machine has: the ion ripper and blue hyperblaster of its soldiers, its
laser soldier's beam, its gladiator's phalanx.
*/

#include "g_local.h"

static void Xatrix_MuzzleFlash (edict_t *self, vec3_t start, int flashtype)
{
	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (flashtype);
	gi.multicast (start, MULTICAST_PVS);
}

/*
=================
The blue hyperblaster: a bolt like the blaster's
=================
*/
void blaster_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);

void fire_blueblaster (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect)
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
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->s.modelindex = gi.modelindex ("models/objects/blaser/tris.md2");
	bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
	bolt->owner = self;
	bolt->touch = blaster_touch;
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

void monster_fire_blueblaster (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int flashtype, int effect)
{
	fire_blueblaster (self, start, dir, damage, speed, effect);
	Xatrix_MuzzleFlash (self, start, flashtype);
}

/*
=================
The ion ripper: a boomerang that goes off every wall it meets, for three
seconds, and gives off sparks when it is done
=================
*/
void ionripper_sparks (edict_t *self)
{
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_WELDING_SPARKS);
	gi.WriteByte (0);
	gi.WritePosition (self->s.origin);
	gi.WriteDir (vec3_origin);
	gi.WriteByte (0xe4 + (rand() & 3));
	gi.multicast (self->s.origin, MULTICAST_PVS);

	G_FreeEdict (self);
}

void ionripper_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}
	if (self->owner->client)
		PlayerNoise (self->owner, self->s.origin, PNOISE_IMPACT);
	if (other->takedamage)
	{
		T_Damage (other, self, self->owner, self->velocity, self->s.origin,
			plane ? plane->normal : vec3_origin, self->dmg, 1, DAMAGE_ENERGY, MOD_RIPPER);
		G_FreeEdict (self);
	}
}

void fire_ionripper (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect)
{
	edict_t	*ion;
	trace_t	tr;

	VectorNormalize (dir);

	ion = G_Spawn ();
	VectorCopy (start, ion->s.origin);
	VectorCopy (start, ion->s.old_origin);
	vectoangles (dir, ion->s.angles);
	VectorScale (dir, speed, ion->velocity);
	ion->movetype = MOVETYPE_WALLBOUNCE;
	ion->clipmask = MASK_SHOT;
	ion->solid = SOLID_BBOX;
	ion->s.effects |= effect;
	ion->s.renderfx |= RF_FULLBRIGHT;
	VectorClear (ion->mins);
	VectorClear (ion->maxs);
	ion->s.modelindex = gi.modelindex ("models/objects/boomrang/tris.md2");
	ion->s.sound = gi.soundindex ("misc/lasfly.wav");
	ion->owner = self;
	ion->touch = ionripper_touch;
	ion->nextthink = level.time + 3;
	ion->think = ionripper_sparks;
	ion->dmg = damage;
	ion->dmg_radius = 100;
	ion->classname = "ionripper";
	gi.linkentity (ion);

	tr = gi.trace (self->s.origin, NULL, NULL, ion->s.origin, ion, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA (ion->s.origin, -10, dir, ion->s.origin);
		ion->touch (ion, tr.ent, NULL, NULL);
	}
}

void monster_fire_ionripper (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int flashtype, int effect)
{
	fire_ionripper (self, start, dir, damage, speed, effect);
	Xatrix_MuzzleFlash (self, start, flashtype);
}

/*
=================
The laser soldier's beam: a red laser for a frame, from start toward its
enemy, through monsters and players to the wall, hurting each it goes
through
=================
*/
#define DABEAM_SPARKS	0x80000000

void dabeam_hit (edict_t *self)
{
	edict_t	*ignore;
	vec3_t	start, end;
	trace_t	tr;

	ignore = self;
	VectorCopy (self->s.origin, start);
	VectorMA (start, 2048, self->movedir, end);

	while (1)
	{
		tr = gi.trace (start, NULL, NULL, end, ignore,
			CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_DEADMONSTER);
		if (!tr.ent)
			break;

		if (tr.ent->takedamage && !(tr.ent->flags & FL_IMMUNE_LASER) && tr.ent != self->owner)
			T_Damage (tr.ent, self, self->owner, self->movedir, tr.endpos, vec3_origin,
				self->dmg, skill->value, DAMAGE_ENERGY, MOD_TARGET_LASER);

		// at something no monster or player, or immune: done
		if (!(tr.ent->svflags & SVF_MONSTER) && !tr.ent->client)
		{
			if (self->spawnflags & DABEAM_SPARKS)
			{
				self->spawnflags &= ~DABEAM_SPARKS;
				gi.WriteByte (svc_temp_entity);
				gi.WriteByte (TE_LASER_SPARKS);
				gi.WriteByte (10);
				gi.WritePosition (tr.endpos);
				gi.WriteDir (tr.plane.normal);
				gi.WriteByte (self->s.skinnum);
				gi.multicast (tr.endpos, MULTICAST_PVS);
			}
			break;
		}
		ignore = tr.ent;
		VectorCopy (tr.endpos, start);
	}

	VectorCopy (tr.endpos, self->s.old_origin);
	self->nextthink = level.time + FRAMETIME;
	self->think = G_FreeEdict;
}

void monster_dabeam (edict_t *self)
{
	vec3_t	last_movedir, point;

	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_NOT;
	self->s.renderfx |= RF_BEAM|RF_TRANSLUCENT;
	self->s.modelindex = 1;
	self->s.frame = 2;
	self->s.skinnum = 0xf2f2f0f0;	// red

	if (self->enemy)
	{
		VectorCopy (self->movedir, last_movedir);
		VectorMA (self->enemy->absmin, 0.5, self->enemy->size, point);
		VectorSubtract (point, self->s.origin, self->movedir);
		VectorNormalize (self->movedir);
		if (!VectorCompare (self->movedir, last_movedir))
			self->spawnflags |= DABEAM_SPARKS;
	}
	else
		G_SetMovedir (self->s.angles, self->movedir);

	self->think = dabeam_hit;
	self->nextthink = level.time + FRAMETIME;
	VectorSet (self->mins, -8, -8, -8);
	VectorSet (self->maxs, 8, 8, 8);
	gi.linkentity (self);

	self->spawnflags |= DABEAM_SPARKS | 1;
	self->svflags &= ~SVF_NOCLIENT;
}

/*
=================
The phalanx: a ball of plasma that bursts where it hits
=================
*/
void plasma_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	origin;

	if (other == ent->owner)
		return;
	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}
	if (ent->owner->client)
		PlayerNoise (ent->owner, ent->s.origin, PNOISE_IMPACT);

	// the explosion, a little back
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	if (other->takedamage)
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin,
			plane ? plane->normal : vec3_origin, ent->dmg, 0, 0, MOD_PHALANX);
	T_RadiusDamage (ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_PHALANX);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (ent->waterlevel ? TE_ROCKET_EXPLOSION_WATER : TE_PLASMA_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	G_FreeEdict (ent);
}

void fire_plasma (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*plasma;

	plasma = G_Spawn ();
	VectorCopy (start, plasma->s.origin);
	VectorCopy (dir, plasma->movedir);
	vectoangles (dir, plasma->s.angles);
	VectorScale (dir, speed, plasma->velocity);
	plasma->movetype = MOVETYPE_FLYMISSILE;
	plasma->clipmask = MASK_SHOT;
	plasma->solid = SOLID_BBOX;
	VectorClear (plasma->mins);
	VectorClear (plasma->maxs);
	plasma->owner = self;
	plasma->touch = plasma_touch;
	plasma->nextthink = level.time + 8000.0f / speed;
	plasma->think = G_FreeEdict;
	plasma->dmg = damage;
	plasma->radius_dmg = radius_damage;
	plasma->dmg_radius = damage_radius;
	plasma->s.sound = gi.soundindex ("weapons/rockfly.wav");
	plasma->s.modelindex = gi.modelindex ("sprites/s_photon.sp2");
	plasma->s.effects |= EF_PLASMA | EF_ANIM_ALLFAST;
	plasma->classname = "plasma";
	gi.linkentity (plasma);
}
