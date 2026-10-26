// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

FLYER, the 2023 re-release's

Its m_flyer.cpp, in C, for its own maps (RR_Map): a flyer that flies as
the re-release's do (AI_ALTERNATE_FLY), circling about you as a buzzard
and now and then holding a spot to fire from, makes fly-by slashes on
its thrusters, glances off other flyers in sparks, and gibs into pieces
of its own; and Ground Zero's kamikaze, which flies straight at you and
blows up. Its frames are 1997's. m_flyer.c is 1997's, for every other
map.

What it does at 40 Hz is done at this game's 10.
==============================================================================
*/

#include "g_local.h"
#include "m_flyer.h"

qboolean visible (edict_t *self, edict_t *other);

static int	sound_sight;
static int	sound_idle;
static int	sound_pain1;
static int	sound_pain2;
static int	sound_slash;
static int	sound_sproing;
static int	sound_die;

static void flyer_check_melee (edict_t *self);
static void flyer_loop_melee (edict_t *self);
static void flyer_kamikaze (edict_t *self);
static void flyer_kamikaze_check (edict_t *self);
static void flyer_run (edict_t *self);
static void flyer_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);

static void flyer_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void flyer_idle (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static void flyer_pop_blades (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_sproing, 1, ATTN_NORM, 0);
}

static mframe_t flyer_frames_stand[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
static mmove_t flyer_move_stand = { FRAME_stand01, FRAME_stand45, flyer_frames_stand, NULL };

static mframe_t flyer_frames_walk[] = {
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 }
};
static mmove_t flyer_move_walk = { FRAME_stand01, FRAME_stand45, flyer_frames_walk, NULL };

static mframe_t flyer_frames_run[] = {
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 }
};
static mmove_t flyer_move_run = { FRAME_stand01, FRAME_stand45, flyer_frames_run, NULL };

static mframe_t flyer_frames_kamizake[] = {
	{ ai_charge, 40, flyer_kamikaze_check },
	{ ai_charge, 40, flyer_kamikaze_check },
	{ ai_charge, 40, flyer_kamikaze_check },
	{ ai_charge, 40, flyer_kamikaze_check },
	{ ai_charge, 40, flyer_kamikaze_check }
};
static mmove_t flyer_move_kamikaze = { FRAME_rollr02, FRAME_rollr06, flyer_frames_kamizake, flyer_kamikaze };

static void flyer_run (edict_t *self)
{
	if (self->mass > 50)
		self->monsterinfo.currentmove = &flyer_move_kamikaze;
	else if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &flyer_move_stand;
	else
		self->monsterinfo.currentmove = &flyer_move_run;
}

static void flyer_walk (edict_t *self)
{
	if (self->mass > 50)
		flyer_run (self);
	else
		self->monsterinfo.currentmove = &flyer_move_walk;
}

static void flyer_stand (edict_t *self)
{
	if (self->mass > 50)
		flyer_run (self);
	else
		self->monsterinfo.currentmove = &flyer_move_stand;
}

static void flyer_kamikaze_explode (edict_t *self)
{
	vec3_t	dir;

	if (self->monsterinfo.commander && self->monsterinfo.commander->inuse &&
		!strcmp (self->monsterinfo.commander->classname, "monster_carrier"))
		self->monsterinfo.commander->monsterinfo.monster_slots++;

	VectorClear (dir);
	if (self->enemy)
	{
		VectorSubtract (self->enemy->s.origin, self->s.origin, dir);
		T_Damage (self->enemy, self, self, dir, self->s.origin, vec3_origin, 50, 50, DAMAGE_RADIUS, MOD_UNKNOWN);
	}

	flyer_die (self, NULL, NULL, 0, dir);
}

static void flyer_kamikaze (edict_t *self)
{
	self->monsterinfo.currentmove = &flyer_move_kamikaze;
}

static void flyer_kamikaze_check (edict_t *self)
{
	vec3_t	v, a;

	// PMM - this needed because we could have gone away before we get here (blocked code)
	if (!self->inuse)
		return;

	if ((!self->enemy) || (!self->enemy->inuse))
	{
		flyer_kamikaze_explode (self);
		return;
	}

	VectorSubtract (self->enemy->s.origin, self->s.origin, v);
	vectoangles (v, a);
	self->s.angles[0] = a[0];

	self->goalentity = self->enemy;

	if (realrange (self, self->enemy) < 90)
		flyer_kamikaze_explode (self);
}

static mframe_t flyer_frames_pain3[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t flyer_move_pain3 = { FRAME_pain301, FRAME_pain304, flyer_frames_pain3, flyer_run };

static mframe_t flyer_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t flyer_move_pain2 = { FRAME_pain201, FRAME_pain204, flyer_frames_pain2, flyer_run };

static mframe_t flyer_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t flyer_move_pain1 = { FRAME_pain101, FRAME_pain109, flyer_frames_pain1, flyer_run };

static void flyer_fire (edict_t *self, int flash_number)
{
	vec3_t	start;
	vec3_t	forward, right;
	vec3_t	end;
	vec3_t	dir;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);

	VectorCopy (self->enemy->s.origin, end);
	end[2] += self->enemy->viewheight;
	VectorSubtract (end, start, dir);
	VectorNormalize (dir);

	monster_fire_blaster (self, start, dir, 1, 1000, flash_number, (self->s.frame % 4) ? 0 : EF_HYPERBLASTER);
}

static void flyer_fireleft (edict_t *self)
{
	flyer_fire (self, MZ2_FLYER_BLASTER_1);
}

static void flyer_fireright (edict_t *self)
{
	flyer_fire (self, MZ2_FLYER_BLASTER_2);
}

static mframe_t flyer_frames_attack2[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge, -10, flyer_fireleft },	 // left gun
	{ ai_charge, -10, flyer_fireright }, // right gun
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t flyer_move_attack2 = { FRAME_attak201, FRAME_attak217, flyer_frames_attack2, flyer_run };

// PMM
// circle strafe frames: what its carrier's summoned flyers start with
static mframe_t flyer_frames_attack3[] = {
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10, flyer_fireleft },	// left gun
	{ ai_charge, 10, flyer_fireright }, // right gun
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 }
};
static mmove_t flyer_move_attack3 = { FRAME_attak201, FRAME_attak217, flyer_frames_attack3, flyer_run };
// pmm

// for its carrier (m_carrier_rr.c), which sets its summoned ones going
mmove_t *flyer_move_attack3_rr = &flyer_move_attack3;
mmove_t *flyer_move_kamikaze_rr = &flyer_move_kamikaze;

static void flyer_slash_left (edict_t *self)
{
	vec3_t	aim;

	VectorSet (aim, MELEE_DISTANCE_RR, self->mins[0], 0);
	if (!fire_hit_rr (self, aim, 5, 0))
		self->monsterinfo.melee_debounce_time = level.time + 1.5;
	gi.sound (self, CHAN_WEAPON, sound_slash, 1, ATTN_NORM, 0);
}

static void flyer_slash_right (edict_t *self)
{
	vec3_t	aim;

	VectorSet (aim, MELEE_DISTANCE_RR, self->maxs[0], 0);
	if (!fire_hit_rr (self, aim, 5, 0))
		self->monsterinfo.melee_debounce_time = level.time + 1.5;
	gi.sound (self, CHAN_WEAPON, sound_slash, 1, ATTN_NORM, 0);
}

static mframe_t flyer_frames_start_melee[] = {
	{ ai_charge, 0, flyer_pop_blades },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t flyer_move_start_melee = { FRAME_attak101, FRAME_attak106, flyer_frames_start_melee, flyer_loop_melee };

static mframe_t flyer_frames_end_melee[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t flyer_move_end_melee = { FRAME_attak119, FRAME_attak121, flyer_frames_end_melee, flyer_run };

static mframe_t flyer_frames_loop_melee[] = {
	{ ai_charge }, // Loop Start
	{ ai_charge },
	{ ai_charge, 0, flyer_slash_left }, // Left Wing Strike
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, flyer_slash_right }, // Right Wing Strike
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge } // Loop Ends

};
static mmove_t flyer_move_loop_melee = { FRAME_attak107, FRAME_attak118, flyer_frames_loop_melee, flyer_check_melee };

static void flyer_loop_melee (edict_t *self)
{
	self->monsterinfo.currentmove = &flyer_move_loop_melee;
}

static void flyer_set_fly_parameters (edict_t *self, qboolean melee)
{
	if (melee)
	{
		// engage thrusters for a slice
		self->monsterinfo.fly_pinned = false;
		self->monsterinfo.fly_thrusters = true;
		self->monsterinfo.fly_position_time = 0;
		self->monsterinfo.fly_acceleration = 20.f;
		self->monsterinfo.fly_speed = 210.f;
		self->monsterinfo.fly_min_distance = 0.f;
		self->monsterinfo.fly_max_distance = 10.f;
	}
	else
	{
		self->monsterinfo.fly_thrusters = false;
		self->monsterinfo.fly_acceleration = 15.f;
		self->monsterinfo.fly_speed = 165.f;
		self->monsterinfo.fly_min_distance = 45.f;
		self->monsterinfo.fly_max_distance = 200.f;
	}
}

static void flyer_attack (edict_t *self)
{
	float	range;

	if (self->mass > 50)
	{
		flyer_run (self);
		return;
	}

	range = range_to (self, self->enemy);

	if (self->enemy && visible (self, self->enemy) && range <= 225.f && random() > (range / 225.f) * 0.35f)
	{
		// fly-by slicing!
		self->monsterinfo.attack_state = AS_STRAIGHT;
		self->monsterinfo.currentmove = &flyer_move_start_melee;
		flyer_set_fly_parameters (self, true);
	}
	else
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		self->monsterinfo.currentmove = &flyer_move_attack2;
	}

	// [Paril-KEX] for alternate fly mode, sometimes we'll pin us
	// down, kind of like a pseudo-stand ground
	if (!self->monsterinfo.fly_pinned && (rand () & 1) && self->enemy && visible (self, self->enemy))
	{
		self->monsterinfo.fly_pinned = true;
		self->monsterinfo.fly_position_time += 1.7; // make sure there's enough time for attack2/3

		if (rand () & 1)
			VectorMA (self->s.origin, random(), self->velocity, self->monsterinfo.fly_ideal_position); // pin to our current position
		else
			VectorAdd (self->monsterinfo.fly_ideal_position, self->enemy->s.origin, self->monsterinfo.fly_ideal_position); // make un-relative
	}

	// if we're currently pinned, fly_position_time will unpin us eventually
}

static void flyer_melee (edict_t *self)
{
	if (self->mass > 50)
		flyer_run (self);
	else
	{
		self->monsterinfo.currentmove = &flyer_move_start_melee;
		flyer_set_fly_parameters (self, true);
	}
}

static void flyer_check_melee (edict_t *self)
{
	if (range_to (self, self->enemy) <= RANGE_MELEE_RR)
	{
		if (self->monsterinfo.melee_debounce_time <= level.time)
		{
			self->monsterinfo.currentmove = &flyer_move_loop_melee;
			return;
		}
	}

	self->monsterinfo.currentmove = &flyer_move_end_melee;
	flyer_set_fly_parameters (self, false);
}

static void flyer_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	int		n;

	//	pmm	 - kamikaze's don't feel pain
	if (self->mass != 50)
		return;
	// pmm

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	n = rand () % 3;
	if (n == 0)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else if (n == 1)
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	flyer_set_fly_parameters (self, false);

	if (n == 0)
		self->monsterinfo.currentmove = &flyer_move_pain1;
	else if (n == 1)
		self->monsterinfo.currentmove = &flyer_move_pain2;
	else
		self->monsterinfo.currentmove = &flyer_move_pain3;
}

static void flyer_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static gib_def_t flyer_gibs[] = {
	{ 2, "models/objects/gibs/sm_metal/tris.md2" },
	{ 2, "models/objects/gibs/sm_meat/tris.md2" },
	{ 1, "models/monsters/flyer/gibs/base.md2", GIB_SKINNED },
	{ 2, "models/monsters/flyer/gibs/gun.md2", GIB_SKINNED },
	{ 2, "models/monsters/flyer/gibs/wing.md2", GIB_SKINNED },
	{ 1, "models/monsters/flyer/gibs/head.md2", GIB_SKINNED | GIB_HEAD },
	{ 0, NULL }
};

static void flyer_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	gi.sound (self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	self->s.skinnum /= 2;

	ThrowGibs (self, 55, flyer_gibs);

	self->touch = NULL;
}

static qboolean flyer_blocked (edict_t *self, float dist)
{
	// kamikaze = 100, normal = 50
	if (self->mass == 100)
	{
		flyer_kamikaze_check (self);

		// if the above didn't blow us up (i.e. I got blocked by the player)
		if (self->inuse)
			T_Damage (self, self, self, vec3_origin, self->s.origin, vec3_origin, 9999, 100, 0, MOD_UNKNOWN);

		return true;
	}

	return false;
}

static void kamikaze_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	dir;

	VectorCopy (ent->velocity, dir);
	VectorNormalize (dir);
	T_Damage (ent, ent, ent, dir, ent->s.origin, dir, 9999, 100, 0, MOD_UNKNOWN);
}

static void flyer_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	dir;

	if ((other->monsterinfo.aiflags & AI_ALTERNATE_FLY) && (other->flags & FL_FLY) &&
		(ent->monsterinfo.duck_wait_time < level.time))
	{
		ent->monsterinfo.duck_wait_time = level.time + 1;
		ent->monsterinfo.fly_thrusters = false;

		VectorSubtract (ent->s.origin, other->s.origin, dir);
		VectorNormalize (dir);
		VectorScale (dir, 500.f, ent->velocity);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_SPLASH);
		gi.WriteByte (32);
		gi.WritePosition (ent->s.origin);
		gi.WriteDir (dir);
		gi.WriteByte (SPLASH_SPARKS);
		gi.multicast (ent->s.origin, MULTICAST_PVS);
	}
}

void SP_monster_flyer_rr (edict_t *self)
{
	sound_sight = gi.soundindex ("flyer/flysght1.wav");
	sound_idle = gi.soundindex ("flyer/flysrch1.wav");
	sound_pain1 = gi.soundindex ("flyer/flypain1.wav");
	sound_pain2 = gi.soundindex ("flyer/flypain2.wav");
	sound_slash = gi.soundindex ("flyer/flyatck2.wav");
	sound_sproing = gi.soundindex ("flyer/flyatck1.wav");
	sound_die = gi.soundindex ("flyer/flydeth1.wav");

	gi.soundindex ("flyer/flyatck3.wav");

	self->s.modelindex = gi.modelindex ("models/monsters/flyer/tris.md2");

	gi.modelindex ("models/monsters/flyer/gibs/base.md2");
	gi.modelindex ("models/monsters/flyer/gibs/wing.md2");
	gi.modelindex ("models/monsters/flyer/gibs/gun.md2");
	gi.modelindex ("models/monsters/flyer/gibs/head.md2");

	VectorSet (self->mins, -16, -16, -24);
	// PMM - shortened to 16 from 32
	VectorSet (self->maxs, 16, 16, 16);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	self->viewheight = 12;

	self->s.sound = gi.soundindex ("flyer/flyidle1.wav");

	self->health = 50;
	self->mass = 50;

	self->pain = flyer_pain;
	self->die = flyer_die;

	self->monsterinfo.stand = flyer_stand;
	self->monsterinfo.walk = flyer_walk;
	self->monsterinfo.run = flyer_run;
	self->monsterinfo.attack = flyer_attack;
	self->monsterinfo.melee = flyer_melee;
	self->monsterinfo.sight = flyer_sight;
	self->monsterinfo.idle = flyer_idle;
	self->monsterinfo.blocked = flyer_blocked;
	self->monsterinfo.setskin = flyer_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &flyer_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	// Ground Zero's kamikaze
	if (!strcmp (self->classname, "monster_kamikaze"))
		self->s.effects |= EF_ROCKET;

	if (self->s.effects & EF_ROCKET)
	{
		// PMM - normal flyer has mass of 50
		self->mass = 100;
		self->yaw_speed = 5;
		self->touch = kamikaze_touch;
	}
	else
	{
		self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
		self->monsterinfo.fly_buzzard = true;
		flyer_set_fly_parameters (self, false);
		self->touch = flyer_touch;
	}

	flymonster_start (self);
}
