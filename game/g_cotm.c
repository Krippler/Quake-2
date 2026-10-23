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
g_cotm.c -- what the 2023 re-release's Call of the Machine needs of the game.

Its maps are made for the re-release's game, which has the base game, both
mission packs and things of its own in one. Here are its targets and
triggers, from its source (id's quake2-rerelease-dll, GPL) and Ground
Zero's: the hub's unit locks, the bosses' health relays, music, sky,
steam, the hub's eyes. Its monsters, weapons and items that this game has
not got stand in as the nearest of its own (G_StandIn), so that every level
can be played through; which they are is in the table below.
*/

#include "g_local.h"

/*
==============================================================================

target_crossunit_trigger, target_crossunit_target

The hub's locks: a unit's last map uses a target_crossunit_trigger, whose
spawnflags are kept in game.cross_unit_flags; at the hub each
target_crossunit_target whose flags are all set uses its targets as the
map starts -- the door to the next unit, at last the boss's.

==============================================================================
*/

#define CROSS_UNIT_MASK	0x00ff00ff	// not the skill bits between

void crossunit_trigger_use (edict_t *self, edict_t *other, edict_t *activator)
{
	game.cross_unit_flags |= self->spawnflags & CROSS_UNIT_MASK;
	G_FreeEdict (self);
}

void SP_target_crossunit_trigger (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}
	self->svflags = SVF_NOCLIENT;
	self->use = crossunit_trigger_use;
}

void crossunit_target_think (edict_t *self)
{
	int		need = self->spawnflags & CROSS_UNIT_MASK;

	if (need == (game.cross_unit_flags & need))
	{
		G_UseTargets (self, self);
		G_FreeEdict (self);
	}
}

void SP_target_crossunit_target (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}
	if (!self->delay)
		self->delay = 1;
	self->svflags = SVF_NOCLIENT;
	self->think = crossunit_target_think;
	self->nextthink = level.time + self->delay;
}

/*
==============================================================================

trigger_health_relay, and the monsters' "healthtarget"

A monster with a healthtarget uses it each time it is hurt, and as it dies
(G_HealthTarget, from T_Damage); a trigger_health_relay of that name fires
its targets, once, when the monster is down to "speed" of its health: the
bosses' reinforcements and stages.

==============================================================================
*/

void G_HealthTarget (edict_t *self)
{
	char	*target;

	if (!self->healthtarget)
		return;
	target = self->target;
	self->target = self->healthtarget;
	G_UseTargets (self, self->enemy);
	if (self->inuse)
		self->target = target;
}

void trigger_health_relay_use (edict_t *self, edict_t *other, edict_t *activator)
{
	float	f;

	if (!other || other->max_health <= 0)
		return;
	f = (float)other->health / other->max_health;
	if (f > self->speed)
		return;		// not yet
	G_UseTargets (self, activator);
	G_FreeEdict (self);
}

void SP_trigger_health_relay (edict_t *self)
{
	if (!self->targetname || self->speed < 0 || self->speed > 100)
	{
		gi.dprintf ("trigger_health_relay at %s: no targetname, or a bad speed\n", vtos(self->s.origin));
		G_FreeEdict (self);
		return;
	}
	self->svflags |= SVF_NOCLIENT;
	self->use = trigger_health_relay_use;
}

/*
==============================================================================

target_anger (Ground Zero's)

Makes the monsters it targets go for the entity its killtarget names.

==============================================================================
*/

void target_anger_use (edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t	*target, *t;

	target = G_Find (NULL, FOFS(targetname), self->killtarget);
	if (!target)
		return;
	// something that is no monster: made one, so that it can be fought
	if (!(target->svflags & SVF_MONSTER))
	{
		target->monsterinfo.aiflags |= AI_GOOD_GUY;
		target->svflags |= SVF_MONSTER;
		target->health = 300;
	}
	t = NULL;
	while ((t = G_Find (t, FOFS(targetname), self->target)) != NULL)
	{
		if (t == self || !t->use || t->health <= 0)
			continue;
		t->enemy = target;
		FoundTarget (t);
		if (!self->inuse)
			return;
	}
}

void SP_target_anger (edict_t *self)
{
	if (!self->target || !self->killtarget)
	{
		gi.dprintf ("target_anger at %s without a target or a killtarget\n", vtos(self->s.origin));
		G_FreeEdict (self);
		return;
	}
	self->use = target_anger_use;
	self->svflags = SVF_NOCLIENT;
}

/*
==============================================================================

target_music, target_sky, target_story

==============================================================================
*/

void use_target_music (edict_t *self, edict_t *other, edict_t *activator)
{
	gi.configstring (CS_CDTRACK, va("%i", self->sounds));
}

void SP_target_music (edict_t *self)
{
	self->use = use_target_music;
	self->svflags = SVF_NOCLIENT;
}

// count: 1 a rotation, 4 an axis; the client takes up the change (cl_parse.c)
void use_target_sky (edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->map)
		gi.configstring (CS_SKY, self->map);
	if (self->count & 1)
		gi.configstring (CS_SKYROTATE, va("%f", self->accel));
	if (self->count & 4)
		gi.configstring (CS_SKYAXIS, va("%f %f %f",
			self->movedir[0], self->movedir[1], self->movedir[2]));
}

void SP_target_sky (edict_t *self)
{
	self->use = use_target_sky;
	self->svflags = SVF_NOCLIENT;
	if (st.sky && st.sky[0])
		self->map = st.sky;
	if (st.skyaxis[0] || st.skyaxis[1] || st.skyaxis[2])
	{
		self->count |= 4;
		VectorCopy (st.skyaxis, self->movedir);
	}
	if (st.skyrotate)
	{
		self->count |= 1;
		self->accel = st.skyrotate;
	}
}

// the re-release shows its "message" over the screen till another clears it
void use_target_story (edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t	*player;
	int		i;

	if (!self->message || !self->message[0])
		return;
	for (i=0 ; i<game.maxclients ; i++)
	{
		player = g_edicts + 1 + i;
		if (player->inuse && player->client)
			gi.centerprintf (player, "%s", self->message);
	}
}

void SP_target_story (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}
	self->use = use_target_story;
	self->svflags = SVF_NOCLIENT;
}

/*
==============================================================================

target_steam (Ground Zero's)

Particles with a speed in a line, each time it is used, or for "wait"
seconds once.

==============================================================================
*/

void use_target_steam (edict_t *self, edict_t *other, edict_t *activator)
{
	static int	nextid;
	vec3_t		point;

	if (nextid > 20000)
		nextid = nextid % 20000;
	nextid++;

	// the wait from the func_timer using it, unless it has one
	if (!self->wait)
		self->wait = other ? other->wait * 1000 : 1000;

	if (self->enemy)
	{
		VectorMA (self->enemy->absmin, 0.5, self->enemy->size, point);
		VectorSubtract (point, self->s.origin, self->movedir);
		VectorNormalize (self->movedir);
	}

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_STEAM);
	gi.WriteShort (self->wait > 100 ? nextid : -1);
	gi.WriteByte (self->count);
	gi.WritePosition (self->s.origin);
	gi.WriteDir (self->movedir);
	gi.WriteByte (self->sounds & 0xff);
	gi.WriteShort ((short)self->style);
	if (self->wait > 100)
		gi.WriteLong ((int)self->wait);
	gi.multicast (self->s.origin, MULTICAST_PVS);
}

void target_steam_start (edict_t *self)
{
	edict_t	*ent;

	self->use = use_target_steam;
	if (self->target)
	{
		ent = G_Find (NULL, FOFS(targetname), self->target);
		if (!ent)
			gi.dprintf ("%s at %s: %s is a bad target\n", self->classname,
				vtos(self->s.origin), self->target);
		self->enemy = ent;
	}
	else
		G_SetMovedir (self->s.angles, self->movedir);

	if (!self->count)
		self->count = 32;
	if (!self->style)
		self->style = 75;
	if (!self->sounds)
		self->sounds = 8;
	if (self->wait)
		self->wait *= 1000;		// milliseconds
	self->sounds &= 0xff;
	self->count &= 0xff;
	self->svflags = SVF_NOCLIENT;
	gi.linkentity (self);
}

void SP_target_steam (edict_t *self)
{
	self->style = self->speed;	// its speed, in a field of its own
	if (self->target)
	{
		self->think = target_steam_start;
		self->nextthink = level.time + 1;
	}
	else
		target_steam_start (self);
}

/*
==============================================================================

func_eye

A brush model, the hub's eyes, that turns to watch the nearest player in
its "radius" and within its cone of view, using its targets the first time
it sees one.

==============================================================================
*/

#define FUNC_EYE_FIRED	0x20000

void func_eye_think (edict_t *self)
{
	edict_t	*player, *closest = NULL;
	vec3_t	dir, fwd, rgt, up, eye, wanted;
	float	dist, closest_dist = 0, current, ideal, move;
	int		i;

	for (i=0 ; i<game.maxclients ; i++)
	{
		player = g_edicts + 1 + i;
		if (!player->inuse || !player->client || player->health <= 0)
			continue;
		VectorSubtract (player->s.origin, self->s.origin, dir);
		dist = VectorNormalize (dir);
		if (DotProduct (dir, self->movedir) < self->yaw_speed || dist >= self->dmg_radius)
			continue;
		if (!closest || dist < closest_dist)
		{
			closest = player;
			closest_dist = dist;
		}
	}
	self->enemy = closest;

	AngleVectors (self->s.angles, fwd, rgt, up);
	VectorCopy (self->s.origin, eye);
	VectorMA (eye, self->move_origin[0], fwd, eye);
	VectorMA (eye, self->move_origin[1], rgt, eye);
	VectorMA (eye, self->move_origin[2], up, eye);

	if (self->enemy)
	{
		if (!(self->spawnflags & FUNC_EYE_FIRED))
		{
			G_UseTargets (self, self->enemy);
			self->spawnflags |= FUNC_EYE_FIRED;
		}
		VectorSubtract (self->enemy->s.origin, eye, dir);
		VectorNormalize (dir);
		vectoangles (dir, wanted);
		self->s.frame = 2;
		self->timestamp = level.time + self->wait;
	}
	else if (self->timestamp <= level.time)
	{
		VectorCopy (self->move_angles, wanted);	// back to where it looked
		self->s.frame = 0;
	}
	else
		VectorCopy (self->s.angles, wanted);

	for (i=0 ; i<2 ; i++)
	{
		current = anglemod (self->s.angles[i]);
		ideal = wanted[i];
		if (current == ideal)
			continue;
		move = ideal - current;
		if (ideal > current)
		{
			if (move >= 180)
				move -= 360;
		}
		else if (move <= -180)
			move += 360;
		if (move > self->speed)
			move = self->speed;
		else if (move < -self->speed)
			move = -self->speed;
		self->s.angles[i] = anglemod (current + move);
	}
	self->nextthink = level.time + FRAMETIME;
}

void func_eye_setup (edict_t *self)
{
	edict_t	*eye = G_PickTarget (self->pathtarget);

	if (!eye)
		gi.dprintf ("func_eye at %s: bad pathtarget\n", vtos(self->s.origin));
	else
		VectorSubtract (eye->s.origin, self->s.origin, self->move_origin);
	VectorCopy (self->move_origin, self->movedir);
	VectorNormalize (self->movedir);
	self->think = func_eye_think;
	self->nextthink = level.time + FRAMETIME;
}

void SP_func_eye (edict_t *ent)
{
	vec3_t	right, up, o;

	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	gi.setmodel (ent, ent->model);

	ent->dmg_radius = st.radius ? st.radius : 512;
	if (!ent->speed)
		ent->speed = 45;
	if (!ent->yaw_speed)
		ent->yaw_speed = 0.5;	// its cone of view
	ent->speed *= FRAMETIME;
	VectorCopy (ent->s.angles, ent->move_angles);
	ent->wait = 1.0;

	if (ent->pathtarget)
		ent->think = func_eye_setup;
	else
	{
		ent->think = func_eye_think;
		AngleVectors (ent->move_angles, ent->movedir, right, up);
		VectorCopy (ent->move_origin, o);
		VectorScale (ent->movedir, o[0], ent->move_origin);
		VectorMA (ent->move_origin, o[1], right, ent->move_origin);
		VectorMA (ent->move_origin, o[2], up, ent->move_origin);
	}
	ent->nextthink = level.time + FRAMETIME;
	gi.linkentity (ent);
}

/*
==============================================================================

func_animation

A solid brush model whose using switches its textures' animation. Its
animation is the re-release's; here it stays as it is.

==============================================================================
*/

void SP_func_animation (edict_t *self)
{
	self->movetype = MOVETYPE_PUSH;
	gi.setmodel (self, self->model);
	self->solid = SOLID_BSP;
	gi.linkentity (self);
}

/*
==============================================================================

What has no part here: its health bars, autosaves, fog and flashlight
triggers are the re-release's engine's. Taken out quietly.

==============================================================================
*/

void SP_cotm_none (edict_t *self)
{
	G_FreeEdict (self);
}

// The Reckoning's misc_nuke, which kills everyone when used
void target_killplayers_use (edict_t *self, edict_t *other, edict_t *activator);

void SP_misc_nuke (edict_t *self)
{
	self->use = target_killplayers_use;
	self->svflags = SVF_NOCLIENT;
}

/*
==============================================================================

Stand-ins

The mission packs' monsters, weapons and items, and the re-release's own,
spawned as the nearest this game has: the levels can be played through,
though not as they were made to be. The entity keeps its keys -- its
targetname, its spawnflags, the item it drops -- and takes the stand-in's
classname.

==============================================================================
*/

typedef struct
{
	char	*name, *as;
} standin_t;

static standin_t	standins[] =
{
	// monsters
	{"monster_gekk",				"monster_mutant"},		// The Reckoning's
	{"monster_boss5",				"monster_supertank"},
	{"monster_stalker",				"monster_parasite"},	// Ground Zero's
	{"monster_medic_commander",		"monster_medic"},
	{"monster_carrier",				"monster_boss2"},
	{"monster_daedalus",			"monster_hover"},
	{"monster_guncmdr",				"monster_gunner"},		// the re-release's
	{"monster_shambler",			"monster_berserk"},
	{"monster_arachnid",			"monster_gladiator"},

	// weapons
	{"weapon_chainfist",			"weapon_supershotgun"},
	{"weapon_heatbeam",				"weapon_hyperblaster"},
	{"weapon_plasmabeam",			"weapon_hyperblaster"},
	{"weapon_disintegrator",		"weapon_railgun"},
	{"weapon_phalanx",				"weapon_rocketlauncher"},

	// ammunition and items
	{"ammo_tesla",					"ammo_grenades"},
	{"ammo_magslug",				"ammo_rockets"},
	{"ammo_disruptor",				"ammo_slugs"},
	{"item_double",					"item_quad"},
	{"item_quadfire",				"item_quad"},
	{"item_legacy_head",			"item_ancient_head"},

	{NULL, NULL}
};

/*
The stand-in's classname for one this game has no spawn function for, or
NULL.
*/
char *G_StandIn (char *classname)
{
	standin_t	*s;

	for (s = standins ; s->name ; s++)
		if (!strcmp (s->name, classname))
			return s->as;
	return NULL;
}
