// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

TECHNICIAN, the 2023 re-release's

Its m_float.cpp, in C, for its own maps (RR_Map): a technician that
flies as the re-release's do (AI_ALTERNATE_FLY), keeping in close for
its claws and its zapper, fires its blaster now straight and now
circling, may lie in disguise till it wakes, and gibs into pieces of
its own. Its frames are 1997's. m_float.c is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10.
==============================================================================
*/

#include "g_local.h"
#include "m_float.h"

#define SPAWNFLAG_FLOATER_DISGUISE	8

static int	sound_attack2;
static int	sound_attack3;
static int	sound_death1;
static int	sound_idle;
static int	sound_pain1;
static int	sound_pain2;
static int	sound_sight;

static void floater_dead (edict_t *self);
static void floater_run (edict_t *self);
static void floater_wham (edict_t *self);
static void floater_zap (edict_t *self);

static void floater_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void floater_idle (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static void floater_fire_blaster (edict_t *self)
{
	vec3_t	start;
	vec3_t	forward, right;
	vec3_t	end;
	vec3_t	dir;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_FLOAT_BLASTER_1], forward, right, start);

	VectorCopy (self->enemy->s.origin, end);
	end[2] += self->enemy->viewheight;
	VectorSubtract (end, start, dir);
	VectorNormalize (dir);

	monster_fire_blaster (self, start, dir, 1, 1000, MZ2_FLOAT_BLASTER_1, (self->s.frame % 4) ? 0 : EF_HYPERBLASTER);
}

static mframe_t floater_frames_stand1[] = {
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
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
static mmove_t floater_move_stand1 = { FRAME_stand101, FRAME_stand152, floater_frames_stand1, NULL };

static mframe_t floater_frames_stand2[] = {
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
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
static mmove_t floater_move_stand2 = { FRAME_stand201, FRAME_stand252, floater_frames_stand2, NULL };

static mframe_t floater_frames_pop[] = {
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{},
	{}
};
static mmove_t floater_move_pop = { FRAME_actvat05, FRAME_actvat31, floater_frames_pop, floater_run };

static mframe_t floater_frames_disguise[] = {
	{ ai_stand }
};
static mmove_t floater_move_disguise = { FRAME_actvat01, FRAME_actvat01, floater_frames_disguise, NULL };

static void floater_stand (edict_t *self)
{
	if (self->monsterinfo.currentmove == &floater_move_disguise)
		self->monsterinfo.currentmove = &floater_move_disguise;
	else if (random() <= 0.5f)
		self->monsterinfo.currentmove = &floater_move_stand1;
	else
		self->monsterinfo.currentmove = &floater_move_stand2;
}

static mframe_t floater_frames_attack1[] = {
	{ ai_charge }, // Blaster attack
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, floater_fire_blaster }, // BOOM (0, -25.8, 32.5)	-- LOOP Starts
	{ ai_charge, 0, floater_fire_blaster },
	{ ai_charge, 0, floater_fire_blaster },
	{ ai_charge, 0, floater_fire_blaster },
	{ ai_charge, 0, floater_fire_blaster },
	{ ai_charge, 0, floater_fire_blaster },
	{ ai_charge, 0, floater_fire_blaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge } //							-- LOOP Ends
};
static mmove_t floater_move_attack1 = { FRAME_attak101, FRAME_attak114, floater_frames_attack1, floater_run };

static mframe_t floater_frames_attack1a[] = {
	{ ai_charge, 10 }, // Blaster attack
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10, floater_fire_blaster }, // BOOM (0, -25.8, 32.5)	-- LOOP Starts
	{ ai_charge, 10, floater_fire_blaster },
	{ ai_charge, 10, floater_fire_blaster },
	{ ai_charge, 10, floater_fire_blaster },
	{ ai_charge, 10, floater_fire_blaster },
	{ ai_charge, 10, floater_fire_blaster },
	{ ai_charge, 10, floater_fire_blaster },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 },
	{ ai_charge, 10 } //							-- LOOP Ends
};
static mmove_t floater_move_attack1a = { FRAME_attak101, FRAME_attak114, floater_frames_attack1a, floater_run };

static mframe_t floater_frames_attack2[] = {
	{ ai_charge }, // Claws
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
	{ ai_charge, 0, floater_wham }, // WHAM (0, -45, 29.6)		-- LOOP Starts
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, //							-- LOOP Ends
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t floater_move_attack2 = { FRAME_attak201, FRAME_attak225, floater_frames_attack2, floater_run };

static mframe_t floater_frames_attack3[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, floater_zap }, //								-- LOOP Starts
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
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, //								-- LOOP Ends
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t floater_move_attack3 = { FRAME_attak301, FRAME_attak334, floater_frames_attack3, floater_run };

static mframe_t floater_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t floater_move_pain1 = { FRAME_pain101, FRAME_pain107, floater_frames_pain1, floater_run };

static mframe_t floater_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t floater_move_pain2 = { FRAME_pain201, FRAME_pain208, floater_frames_pain2, floater_run };

static mframe_t floater_frames_walk[] = {
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
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 5 }
};
static mmove_t floater_move_walk = { FRAME_stand101, FRAME_stand152, floater_frames_walk, NULL };

static mframe_t floater_frames_run[] = {
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 },
	{ ai_run, 13 }
};
static mmove_t floater_move_run = { FRAME_stand101, FRAME_stand152, floater_frames_run, NULL };

static void floater_run (edict_t *self)
{
	if (self->monsterinfo.currentmove == &floater_move_disguise)
		self->monsterinfo.currentmove = &floater_move_pop;
	else if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &floater_move_stand1;
	else
		self->monsterinfo.currentmove = &floater_move_run;
}

static void floater_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &floater_move_walk;
}

static void floater_wham (edict_t *self)
{
	vec3_t	aim = { MELEE_DISTANCE_RR, 0, 0 };

	gi.sound (self, CHAN_WEAPON, sound_attack3, 1, ATTN_NORM, 0);

	if (!fire_hit_rr (self, aim, 5 + rand () % 6, -50))
		self->monsterinfo.melee_debounce_time = level.time + 3;
}

static void floater_zap (edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	origin;
	vec3_t	dir;
	vec3_t	offset;

	VectorSubtract (self->enemy->s.origin, self->s.origin, dir);

	AngleVectors (self->s.angles, forward, right, NULL);
	// FIXME use a flash and replace these two lines with the commented one
	VectorSet (offset, 18.5f, -0.9f, 10);
	G_ProjectSource (self->s.origin, offset, forward, right, origin);

	gi.sound (self, CHAN_WEAPON, sound_attack2, 1, ATTN_NORM, 0);

	// FIXME use the flash, Luke
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_SPLASH);
	gi.WriteByte (32);
	gi.WritePosition (origin);
	gi.WriteDir (dir);
	gi.WriteByte (SPLASH_SPARKS);
	gi.multicast (origin, MULTICAST_PVS);

	T_Damage (self->enemy, self, self, dir, self->enemy->s.origin, vec3_origin, 5 + rand () % 6, -10, DAMAGE_ENERGY, MOD_UNKNOWN);
}

static void floater_attack (edict_t *self)
{
	float	chance = 0.5f;

	if (random() > chance)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		self->monsterinfo.currentmove = &floater_move_attack1;
	}
	else // circle strafe
	{
		if (random() <= 0.5f) // switch directions
			self->monsterinfo.lefty = !self->monsterinfo.lefty;
		self->monsterinfo.attack_state = AS_SLIDING;
		self->monsterinfo.currentmove = &floater_move_attack1a;
	}
}

static void floater_melee (edict_t *self)
{
	if (random() < 0.5f)
		self->monsterinfo.currentmove = &floater_move_attack3;
	else
		self->monsterinfo.currentmove = &floater_move_attack2;
}

static void floater_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	int		n;

	if (level.time < self->pain_debounce_time)
		return;

	// no pain anims if poppin'
	if (self->monsterinfo.currentmove == &floater_move_disguise ||
		self->monsterinfo.currentmove == &floater_move_pop)
		return;

	n = rand () % 3;
	if (n == 0)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	self->pain_debounce_time = level.time + 3;

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	if (n == 0)
		self->monsterinfo.currentmove = &floater_move_pain1;
	else
		self->monsterinfo.currentmove = &floater_move_pain2;
}

static void floater_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static void floater_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0;
	gi.linkentity (self);
}

static gib_def_t floater_gibs[] = {
	{ 2, "models/objects/gibs/sm_metal/tris.md2" },
	{ 3, "models/objects/gibs/sm_meat/tris.md2" },
	{ 1, "models/monsters/float/gibs/piece.md2", GIB_SKINNED },
	{ 1, "models/monsters/float/gibs/gun.md2", GIB_SKINNED },
	{ 1, "models/monsters/float/gibs/base.md2", GIB_SKINNED },
	{ 1, "models/monsters/float/gibs/jar.md2", GIB_SKINNED | GIB_HEAD },
	{ 0, NULL }
};

static void floater_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	gi.sound (self, CHAN_VOICE, sound_death1, 1, ATTN_NORM, 0);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	self->s.skinnum /= 2;

	ThrowGibs (self, 55, floater_gibs);
}

static void float_set_fly_parameters (edict_t *self)
{
	self->monsterinfo.fly_thrusters = false;
	self->monsterinfo.fly_acceleration = 10.f;
	self->monsterinfo.fly_speed = 100.f;
	// Technician gets in closer because he has two melee attacks
	self->monsterinfo.fly_min_distance = 20.f;
	self->monsterinfo.fly_max_distance = 200.f;
}

void SP_monster_floater_rr (edict_t *self)
{
	sound_attack2 = gi.soundindex ("floater/fltatck2.wav");
	sound_attack3 = gi.soundindex ("floater/fltatck3.wav");
	sound_death1 = gi.soundindex ("floater/fltdeth1.wav");
	sound_idle = gi.soundindex ("floater/fltidle1.wav");
	sound_pain1 = gi.soundindex ("floater/fltpain1.wav");
	sound_pain2 = gi.soundindex ("floater/fltpain2.wav");
	sound_sight = gi.soundindex ("floater/fltsght1.wav");

	gi.soundindex ("floater/fltatck1.wav");

	self->s.sound = gi.soundindex ("floater/fltsrch1.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/float/tris.md2");

	gi.modelindex ("models/monsters/float/gibs/base.md2");
	gi.modelindex ("models/monsters/float/gibs/gun.md2");
	gi.modelindex ("models/monsters/float/gibs/jar.md2");
	gi.modelindex ("models/monsters/float/gibs/piece.md2");

	VectorSet (self->mins, -24, -24, -24);
	VectorSet (self->maxs, 24, 24, 48);

	self->health = 200;
	self->gib_health = -80;
	self->mass = 300;

	self->pain = floater_pain;
	self->die = floater_die;

	self->monsterinfo.stand = floater_stand;
	self->monsterinfo.walk = floater_walk;
	self->monsterinfo.run = floater_run;
	self->monsterinfo.attack = floater_attack;
	self->monsterinfo.melee = floater_melee;
	self->monsterinfo.sight = floater_sight;
	self->monsterinfo.idle = floater_idle;
	self->monsterinfo.setskin = floater_setskin;

	gi.linkentity (self);

	if (self->spawnflags & SPAWNFLAG_FLOATER_DISGUISE)
		self->monsterinfo.currentmove = &floater_move_disguise;
	else if (random() <= 0.5f)
		self->monsterinfo.currentmove = &floater_move_stand1;
	else
		self->monsterinfo.currentmove = &floater_move_stand2;

	self->monsterinfo.scale = MODEL_SCALE;

	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	float_set_fly_parameters (self);

	flymonster_start (self);
}
