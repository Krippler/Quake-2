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
	monster_muzzleflash (self, start, flashtype);
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

/*
=================
The heat-seeking rocket, The Reckoning's super tank's (monster_boss5):
it turns to the nearest player it can see in front of it
=================
*/
void rocket_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf);

void heat_think (edict_t *self)
{
	edict_t	*target = NULL, *aquire = NULL;
	vec3_t	vec;
	float	len, oldlen = 0;

	if (level.time > self->timestamp)
	{
		G_FreeEdict (self);		// gone too long without hitting anything
		return;
	}

	while ((target = findradius (target, self->s.origin, 1024)) != NULL)
	{
		if (self->owner == target || !target->client || target->health <= 0)
			continue;
		if (!infront (self, target) || !visible (self, target))
			continue;
		VectorSubtract (self->s.origin, target->s.origin, vec);
		len = VectorLength (vec);
		if (!aquire || len < oldlen)
		{
			aquire = target;
			oldlen = len;
		}
	}

	if (aquire)
	{
		VectorSubtract (aquire->s.origin, self->s.origin, vec);
		vectoangles (vec, self->s.angles);
		VectorNormalize (vec);
		VectorScale (vec, 500, self->velocity);
	}
	self->nextthink = level.time + FRAMETIME;
}

void fire_heat (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*heat;

	heat = G_Spawn ();
	VectorCopy (start, heat->s.origin);
	VectorCopy (dir, heat->movedir);
	vectoangles (dir, heat->s.angles);
	VectorScale (dir, speed, heat->velocity);
	heat->movetype = MOVETYPE_FLYMISSILE;
	heat->clipmask = MASK_SHOT;
	heat->solid = SOLID_BBOX;
	heat->s.effects |= EF_ROCKET;
	VectorClear (heat->mins);
	VectorClear (heat->maxs);
	heat->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	heat->owner = self;
	heat->touch = rocket_touch;
	heat->nextthink = level.time + FRAMETIME;
	heat->think = heat_think;
	heat->timestamp = level.time + 8000.0f / speed;
	heat->dmg = damage;
	heat->radius_dmg = radius_damage;
	heat->dmg_radius = damage_radius;
	heat->s.sound = gi.soundindex ("weapons/rockfly.wav");
	heat->classname = "rocket";
	gi.linkentity (heat);
}

void monster_fire_heat (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int flashtype)
{
	fire_heat (self, start, dir, damage, speed, damage, damage);
	Xatrix_MuzzleFlash (self, start, flashtype);
}

/*
=================
The Reckoning's trap, as the re-release has it: thrown, it opens on the
ground and sucks in the nearest monster in sight (anyone, in
deathmatch), and one light enough it eats, giving back a food cube of
health in proportion; one too heavy, or its half minute up, it blows
up. The gibs of what it ate whirl round it as it chews.
=================
*/
void SP_item_foodcube (edict_t *self);

static void Trap_Gib_Think (edict_t *ent)
{
	vec3_t	forward, right, up, diff, vec, new_origin;
	float	degrees;
	trace_t	tr;

	if (ent->owner->s.frame != 5)
	{
		G_FreeEdict (ent);
		return;
	}

	AngleVectors (ent->owner->s.angles, forward, right, up);

	// rotate us around the center
	degrees = (150.f * FRAMETIME) + ent->owner->delay;
	VectorSubtract (ent->owner->s.origin, ent->s.origin, diff);
	RotatePointAroundVector (vec, up, diff, degrees);
	ent->s.angles[1] += degrees;
	VectorSubtract (ent->owner->s.origin, vec, new_origin);

	tr = gi.trace (ent->s.origin, NULL, NULL, new_origin, ent, MASK_SOLID);
	VectorCopy (tr.endpos, ent->s.origin);

	// pull us towards the trap's center
	VectorNormalize (diff);
	VectorMA (ent->s.origin, 15.0f * FRAMETIME, diff, ent->s.origin);

	ent->watertype = gi.pointcontents (ent->s.origin);
	if (ent->watertype & MASK_WATER)
		ent->waterlevel = 1;

	ent->nextthink = level.time + FRAMETIME;
	gi.linkentity (ent);
}

static void trap_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	BecomeExplosion1 (self);
}

static void Trap_Think (edict_t *ent)
{
	edict_t	*target = NULL;
	edict_t	*best = NULL;
	vec3_t	vec;
	float	len, max_speed, pull;
	float	oldlen = 8000;
	int		i;

	if (ent->timestamp < level.time)
	{
		BecomeExplosion1 (ent);
		return;
	}

	ent->nextthink = level.time + FRAMETIME;

	if (!ent->groundentity)
		return;

	// ok lets do the blood effect
	if (ent->s.frame > 4)
	{
		if (ent->s.frame == 5)
		{
			qboolean spawn = ent->wait == 64;

			ent->wait -= 2;

			if (spawn)
				gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/trapdown.wav"), 1, ATTN_IDLE, 0);

			ent->delay += 2.f;

			if (ent->wait < 19)
				ent->s.frame++;

			return;
		}
		ent->s.frame++;
		if (ent->s.frame == 8)
		{
			ent->nextthink = level.time + 1;
			ent->think = G_FreeEdict;
			ent->s.effects &= ~EF_TRAP;

			best = G_Spawn ();
			best->count = ent->mass;
			SP_item_foodcube (best);
			VectorCopy (ent->s.origin, best->s.origin);
			best->s.origin[2] += 24;
			best->s.angles[YAW] = random () * 360;
			best->velocity[2] = 400;
			best->think (best);
			best->nextthink = 0;
			VectorCopy (best->s.origin, best->s.old_origin);
			gi.linkentity (best);

			gi.sound (best, CHAN_AUTO, gi.soundindex ("misc/fhit3.wav"), 1.f, ATTN_NORM, 0.f);
			return;
		}
		return;
	}

	ent->s.effects &= ~EF_TRAP;
	if (ent->s.frame >= 4)
	{
		ent->s.effects |= EF_TRAP;
		// clear the owner if in deathmatch
		if (deathmatch->value)
			ent->owner = NULL;
	}

	if (ent->s.frame < 4)
	{
		ent->s.frame++;
		return;
	}

	while ((target = findradius (target, ent->s.origin, 256)) != NULL)
	{
		if (target == ent)
			continue;

		// not near flags, teleporters or spawn points in deathmatch
		if (target->classname && deathmatch->value &&
			(!strncmp (target->classname, "info_player_", 12) ||
			 !strcmp (target->classname, "misc_teleporter_dest") ||
			 !strncmp (target->classname, "item_flag_", 10)) &&
			visible (target, ent))
		{
			BecomeExplosion1 (ent);
			return;
		}

		if (!(target->svflags & SVF_MONSTER) && !target->client)
			continue;
		// players only in deathmatch, and not its owner
		if (target->client && (!deathmatch->value || target == ent->teammaster))
			continue;
		if (target->health <= 0)
			continue;
		if (!visible (ent, target))
			continue;
		VectorSubtract (ent->s.origin, target->s.origin, vec);
		len = VectorLength (vec);
		if (!best)
		{
			best = target;
			oldlen = len;
			continue;
		}
		if (len < oldlen)
		{
			oldlen = len;
			best = target;
		}
	}

	// pull the enemy in
	if (best)
	{
		if (best->groundentity)
		{
			best->s.origin[2] += 1;
			best->groundentity = NULL;
		}
		VectorSubtract (ent->s.origin, best->s.origin, vec);
		len = VectorNormalize (vec);

		max_speed = best->client ? 290.f : 150.f;
		pull = max_speed - len;
		if (pull < 64)
			pull = 64;
		if (pull > max_speed)
			pull = max_speed;
		VectorMA (best->velocity, pull, vec, best->velocity);

		ent->s.sound = gi.soundindex ("weapons/trapsuck.wav");

		if (len < 48)
		{
			if (best->mass < 400)
			{
				ent->takedamage = DAMAGE_NO;
				ent->solid = SOLID_NOT;
				ent->die = NULL;

				T_Damage (best, ent, ent->teammaster, vec3_origin, best->s.origin, vec3_origin, 100000, 1, 0, MOD_TRAP);

				ent->enemy = best;
				ent->wait = 64;
				VectorCopy (ent->s.origin, ent->s.old_origin);
				ent->timestamp = level.time + 30;
				ent->accel = best->mass;
				if (deathmatch->value)
					ent->mass = best->mass / 4;
				else
					ent->mass = best->mass / 10;
				// ok spawn the food cube
				ent->s.frame = 5;

				// link up any gibs that this monster may have spawned
				for (i = 0; i < globals.num_edicts; i++)
				{
					edict_t *e = &g_edicts[i];

					if (!e->inuse || !e->classname)
						continue;
					else if (strcmp (e->classname, "gib"))
						continue;
					VectorSubtract (e->s.origin, ent->s.origin, vec);
					if (VectorLength (vec) > 128.f)
						continue;

					e->movetype = MOVETYPE_NONE;
					e->nextthink = level.time + FRAMETIME;
					e->think = Trap_Gib_Think;
					e->owner = ent;
					Trap_Gib_Think (e);
				}
			}
			else
			{
				BecomeExplosion1 (ent);
				return;
			}
		}
	}
}

void fire_trap (edict_t *self, vec3_t start, vec3_t aimdir, int speed)
{
	edict_t	*trap;
	vec3_t	dir;
	vec3_t	forward, right, up;
	float	gravityAdjustment = sv_gravity->value / 800.f;

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	trap = G_Spawn ();
	VectorCopy (start, trap->s.origin);
	VectorScale (aimdir, speed, trap->velocity);
	VectorMA (trap->velocity, (200 + crandom () * 10.0f) * gravityAdjustment, up, trap->velocity);
	VectorMA (trap->velocity, crandom () * 10.0f, right, trap->velocity);
	VectorSet (trap->avelocity, 0, 300, 0);
	trap->movetype = MOVETYPE_BOUNCE;
	trap->clipmask = MASK_SHOT & ~CONTENTS_DEADMONSTER;
	trap->solid = SOLID_BBOX;
	trap->takedamage = DAMAGE_YES;
	VectorSet (trap->mins, -4, -4, 0);
	VectorSet (trap->maxs, 4, 4, 8);
	trap->die = trap_die;
	trap->health = 20;
	trap->s.modelindex = gi.modelindex ("models/weapons/z_trap/tris.md2");
	trap->owner = trap->teammaster = self;
	trap->nextthink = level.time + 1;
	trap->think = Trap_Think;
	trap->classname = "food_cube_trap";
	trap->s.sound = gi.soundindex ("weapons/traploop.wav");
	gi.linkentity (trap);

	trap->timestamp = level.time + 30;
}

/*
The trap's food cube: health, as much as a tenth of what it ate (a
quarter, in deathmatch), past the most
*/
void SP_item_foodcube (edict_t *self)
{
	if (deathmatch->value && ((int)dmflags->value & DF_NO_HEALTH))
	{
		G_FreeEdict (self);
		return;
	}

	self->model = "models/objects/trapfx/tris.md2";
	SpawnItem (self, FindItem ("Health"));
	self->spawnflags |= DROPPED_ITEM;
	self->style = HEALTH_IGNORE_MAX;
	self->classname = "item_foodcube";
	self->s.effects |= EF_GIB;
}
