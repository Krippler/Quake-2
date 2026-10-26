// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

BARRACUDA SHARK, the 2023 re-release's

Its m_flipper.cpp, in C, for its own maps (RR_Map): a shark that swims
as the re-release's do (AI_ALTERNATE_FLY, keeping to water), closes in
to bite, shows a hurt skin, and gibs. Its frames are 1997's. m_flipper.c
is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10.
==============================================================================
*/

#include "g_local.h"
#include "m_flipper.h"

#define FLIPPER_RUN_SPEED	24

static int	sound_chomp;
static int	sound_attack;
static int	sound_pain1;
static int	sound_pain2;
static int	sound_death;
static int	sound_idle;
static int	sound_search;
static int	sound_sight;

static void flipper_run (edict_t *self);
static void flipper_dead (edict_t *self);

static mframe_t flipper_frames_stand[] = {
	{ ai_stand }
};
static mmove_t flipper_move_stand = { FRAME_flphor01, FRAME_flphor01, flipper_frames_stand, NULL };

static void flipper_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &flipper_move_stand;
}

static mframe_t flipper_frames_run[] = {
	{ ai_run, FLIPPER_RUN_SPEED }, // 6
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED }, // 10

	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED }, // 20

	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED },
	{ ai_run, FLIPPER_RUN_SPEED } // 29
};
static mmove_t flipper_move_run_loop = { FRAME_flpver06, FRAME_flpver29, flipper_frames_run, NULL };

static void flipper_run_loop (edict_t *self)
{
	self->monsterinfo.currentmove = &flipper_move_run_loop;
}

static mframe_t flipper_frames_run_start[] = {
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 },
	{ ai_run, 8 }
};
static mmove_t flipper_move_run_start = { FRAME_flpver01, FRAME_flpver06, flipper_frames_run_start, flipper_run_loop };

static void flipper_run (edict_t *self)
{
	self->monsterinfo.currentmove = &flipper_move_run_start;
}

/* Standard Swimming */
static mframe_t flipper_frames_walk[] = {
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 }
};
static mmove_t flipper_move_walk = { FRAME_flphor01, FRAME_flphor24, flipper_frames_walk, NULL };

static void flipper_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &flipper_move_walk;
}

static mframe_t flipper_frames_start_run[] = {
	{ ai_run },
	{ ai_run },
	{ ai_run },
	{ ai_run },
	{ ai_run, 8, flipper_run }
};
static mmove_t flipper_move_start_run = { FRAME_flphor01, FRAME_flphor05, flipper_frames_start_run, NULL };

static void flipper_start_run (edict_t *self)
{
	self->monsterinfo.currentmove = &flipper_move_start_run;
}

static mframe_t flipper_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t flipper_move_pain2 = { FRAME_flppn101, FRAME_flppn105, flipper_frames_pain2, flipper_run };

static mframe_t flipper_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t flipper_move_pain1 = { FRAME_flppn201, FRAME_flppn205, flipper_frames_pain1, flipper_run };

static void flipper_bite (edict_t *self)
{
	vec3_t	aim = { MELEE_DISTANCE_RR, 0, 0 };

	fire_hit_rr (self, aim, 5, 0);
}

static void flipper_preattack (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_chomp, 1, ATTN_NORM, 0);
}

static mframe_t flipper_frames_attack[] = {
	{ ai_charge, 0, flipper_preattack },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, flipper_bite },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, flipper_bite },
	{ ai_charge }
};
static mmove_t flipper_move_attack = { FRAME_flpbit01, FRAME_flpbit20, flipper_frames_attack, flipper_run };

static void flipper_melee (edict_t *self)
{
	self->monsterinfo.currentmove = &flipper_move_attack;
}

static void flipper_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	int		n;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	n = rand () & 1;
	if (n == 0)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	if (n == 0)
		self->monsterinfo.currentmove = &flipper_move_pain1;
	else
		self->monsterinfo.currentmove = &flipper_move_pain2;
}

static void flipper_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static void flipper_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -8);
	VectorSet (self->maxs, 16, 16, 8);
	monster_dead (self);
}

static mframe_t flipper_frames_death[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
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
static mmove_t flipper_move_death = { FRAME_flpdth01, FRAME_flpdth56, flipper_frames_death, flipper_dead };

static void flipper_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static gib_def_t flipper_gibs[] = {
	{ 2, "models/objects/gibs/bone/tris.md2" },
	{ 2, "models/objects/gibs/sm_meat/tris.md2" },
	{ 1, "models/objects/gibs/head2/tris.md2", GIB_HEAD },
	{ 0, NULL }
};

static void flipper_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		ThrowGibs (self, damage, flipper_gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
	self->svflags |= SVF_DEADMONSTER;
	self->monsterinfo.currentmove = &flipper_move_death;
}

static void flipper_set_fly_parameters (edict_t *self)
{
	self->monsterinfo.fly_thrusters = false;
	self->monsterinfo.fly_acceleration = 30.f;
	self->monsterinfo.fly_speed = 110.f;
	// only melee, so get in close
	self->monsterinfo.fly_min_distance = 10.f;
	self->monsterinfo.fly_max_distance = 10.f;
}

void SP_monster_flipper_rr (edict_t *self)
{
	sound_pain1 = gi.soundindex ("flipper/flppain1.wav");
	sound_pain2 = gi.soundindex ("flipper/flppain2.wav");
	sound_death = gi.soundindex ("flipper/flpdeth1.wav");
	sound_chomp = gi.soundindex ("flipper/flpatck1.wav");
	sound_attack = gi.soundindex ("flipper/flpatck2.wav");
	sound_idle = gi.soundindex ("flipper/flpidle1.wav");
	sound_search = gi.soundindex ("flipper/flpsrch1.wav");
	sound_sight = gi.soundindex ("flipper/flpsght1.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/flipper/tris.md2");
	VectorSet (self->mins, -16, -16, -8);
	VectorSet (self->maxs, 16, 16, 20);

	self->health = 50;
	self->gib_health = -30;
	self->mass = 100;

	self->pain = flipper_pain;
	self->die = flipper_die;

	self->monsterinfo.stand = flipper_stand;
	self->monsterinfo.walk = flipper_walk;
	self->monsterinfo.run = flipper_start_run;
	self->monsterinfo.melee = flipper_melee;
	self->monsterinfo.sight = flipper_sight;
	self->monsterinfo.setskin = flipper_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &flipper_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	flipper_set_fly_parameters (self);

	swimmonster_start (self);
}
