// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

GLADIATOR, the 2023 re-release's

Its m_gladiator.cpp, in C, for its own maps (RR_Map): a gladiator whose
railgun hums as he walks, who charges it at where you stand and fires
there, leaves a small safe zone about him where he will rather swing his
cleaver, misses and recovers, and gibs into pieces of his own; and The
Reckoning's phalanx gladiator, with a power shield, firing plasma. Its
frames are 1997's. m_gladiator.c is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10.
==============================================================================
*/

#include "g_local.h"
#include "m_gladiator.h"

qboolean blocked_checkplat (edict_t *self, float dist);

static int	sound_pain1;
static int	sound_pain2;
static int	sound_die;
static int	sound_die2;
static int	sound_gun;
static int	sound_gunb;
static int	sound_cleaver_swing;
static int	sound_cleaver_hit;
static int	sound_cleaver_miss;
static int	sound_idle;
static int	sound_search;
static int	sound_sight;

static void gladiator_run (edict_t *self);
static void gladiator_dead (edict_t *self);

static void gladiator_idle (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static void gladiator_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void gladiator_search (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

static void gladiator_cleaver_swing (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_cleaver_swing, 1, ATTN_NORM, 0);
}

static mframe_t gladiator_frames_stand[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
static mmove_t gladiator_move_stand = { FRAME_stand1, FRAME_stand7, gladiator_frames_stand, NULL };

static void gladiator_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &gladiator_move_stand;
}

static mframe_t gladiator_frames_walk[] = {
	{ ai_walk, 15 },
	{ ai_walk, 7 },
	{ ai_walk, 6 },
	{ ai_walk, 5 },
	{ ai_walk, 2, monster_footstep },
	{ ai_walk },
	{ ai_walk, 2 },
	{ ai_walk, 8 },
	{ ai_walk, 12 },
	{ ai_walk, 8 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 2, monster_footstep },
	{ ai_walk, 2 },
	{ ai_walk, 1 },
	{ ai_walk, 8 }
};
static mmove_t gladiator_move_walk = { FRAME_walk1, FRAME_walk16, gladiator_frames_walk, NULL };

static void gladiator_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &gladiator_move_walk;
}

static mframe_t gladiator_frames_run[] = {
	{ ai_run, 23 },
	{ ai_run, 14 },
	{ ai_run, 14, monster_footstep },
	{ ai_run, 21 },
	{ ai_run, 12 },
	{ ai_run, 13, monster_footstep }
};
static mmove_t gladiator_move_run = { FRAME_run1, FRAME_run6, gladiator_frames_run, NULL };

static void gladiator_run (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &gladiator_move_stand;
	else
		self->monsterinfo.currentmove = &gladiator_move_run;
}

static void GladiatorMelee (edict_t *self)
{
	vec3_t	aim;

	VectorSet (aim, MELEE_DISTANCE_RR, self->mins[0], -4);
	if (fire_hit_rr (self, aim, 20 + rand() % 5, 300))
		gi.sound (self, CHAN_AUTO, sound_cleaver_hit, 1, ATTN_NORM, 0);
	else
	{
		gi.sound (self, CHAN_AUTO, sound_cleaver_miss, 1, ATTN_NORM, 0);
		self->monsterinfo.melee_debounce_time = level.time + 1.5;
	}
}

static mframe_t gladiator_frames_attack_melee[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gladiator_cleaver_swing },
	{ ai_charge },
	{ ai_charge, 0, GladiatorMelee },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gladiator_cleaver_swing },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GladiatorMelee },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t gladiator_move_attack_melee = { FRAME_melee3, FRAME_melee16, gladiator_frames_attack_melee, gladiator_run };

static void gladiator_melee (edict_t *self)
{
	self->monsterinfo.currentmove = &gladiator_move_attack_melee;
}

static void GladiatorGun (edict_t *self)
{
	vec3_t	start, dir, forward, right;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_GLADIATOR_RAILGUN_1], forward, right, start);

	// at where it was aimed
	VectorSubtract (self->pos1, start, dir);
	VectorNormalize (dir);

	monster_fire_railgun (self, start, dir, 50, 100, MZ2_GLADIATOR_RAILGUN_1);
}

static mframe_t gladiator_frames_attack_gun[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GladiatorGun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, monster_footstep },
	{ ai_charge }
};
static mmove_t gladiator_move_attack_gun = { FRAME_attack1, FRAME_attack9, gladiator_frames_attack_gun, gladiator_run };

// The Reckoning's
static void gladbGun (edict_t *self)
{
	vec3_t	start, dir, forward, right;
	int		damage = 35, radius_damage = 45;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_GLADIATOR_RAILGUN_1], forward, right, start);

	// at where it was aimed
	VectorSubtract (self->pos1, start, dir);
	VectorNormalize (dir);

	if (self->s.frame > FRAME_attack3)
	{
		damage /= 2;
		radius_damage /= 2;
	}

	fire_plasma (self, start, dir, damage, 725, radius_damage, radius_damage);

	// aim the next
	VectorCopy (self->enemy->s.origin, self->pos1);
	self->pos1[2] += self->enemy->viewheight;
}

static void gladbGun_check (edict_t *self)
{
	if (skill->value == 3)
		gladbGun (self);
}

static mframe_t gladb_frames_attack_gun[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gladbGun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gladbGun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, gladbGun_check }
};
static mmove_t gladb_move_attack_gun = { FRAME_attack1, FRAME_attack9, gladb_frames_attack_gun, gladiator_run };

static void gladiator_attack (edict_t *self)
{
	vec3_t	v, clear;

	// a small safe zone
	VectorSubtract (self->s.origin, self->enemy->s.origin, v);
	if (VectorLength (v) <= MELEE_DISTANCE_RR + 32 && self->monsterinfo.melee_debounce_time <= level.time)
		return;
	else if (!M_CheckClearShot (self, monster_flash_offset[MZ2_GLADIATOR_RAILGUN_1], clear))
		return;

	// charge up the railgun, at where the enemy stands now
	VectorCopy (self->enemy->s.origin, self->pos1);
	self->pos1[2] += self->enemy->viewheight;
	if (self->style == 1)
	{
		gi.sound (self, CHAN_WEAPON, sound_gunb, 1, ATTN_NORM, 0);
		self->monsterinfo.currentmove = &gladb_move_attack_gun;
	}
	else
	{
		gi.sound (self, CHAN_WEAPON, sound_gun, 1, ATTN_NORM, 0);
		self->monsterinfo.currentmove = &gladiator_move_attack_gun;
	}
}

static mframe_t gladiator_frames_pain[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t gladiator_move_pain = { FRAME_pain2, FRAME_pain5, gladiator_frames_pain, gladiator_run };

static mframe_t gladiator_frames_pain_air[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t gladiator_move_pain_air = { FRAME_painup2, FRAME_painup6, gladiator_frames_pain_air, gladiator_run };

static void gladiator_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (level.time < self->pain_debounce_time)
	{
		if (self->velocity[2] > 100 && self->monsterinfo.currentmove == &gladiator_move_pain)
			self->monsterinfo.currentmove = &gladiator_move_pain_air;
		return;
	}

	self->pain_debounce_time = level.time + 3;

	if (random() < 0.5)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return;		// no pain anims in nightmare

	if (self->velocity[2] > 100)
		self->monsterinfo.currentmove = &gladiator_move_pain_air;
	else
		self->monsterinfo.currentmove = &gladiator_move_pain;
}

static void gladiator_setskin (edict_t *self)
{
	if (self->health < self->max_health / 2)
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

static void gladiator_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

static void gladiator_shrink (edict_t *self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static mframe_t gladiator_frames_death[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, gladiator_shrink },
	{ ai_move, 0, monster_footstep },
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
static mmove_t gladiator_move_death = { FRAME_death2, FRAME_death22, gladiator_frames_death, gladiator_dead };

static gib_def_t gladiator_gibs[] = {
	{ 2, "models/objects/gibs/bone/tris.md2" },
	{ 2, "models/objects/gibs/sm_meat/tris.md2" },
	{ 2, "models/monsters/gladiatr/gibs/thigh.md2", GIB_SKINNED },
	{ 1, "models/monsters/gladiatr/gibs/larm.md2", GIB_SKINNED },
	{ 1, "models/monsters/gladiatr/gibs/rarm.md2", GIB_SKINNED },
	{ 1, "models/monsters/gladiatr/gibs/chest.md2", GIB_SKINNED },
	{ 1, "models/monsters/gladiatr/gibs/head.md2", GIB_SKINNED | GIB_HEAD },
	{ 0, NULL }
};

static void gladiator_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	// his gun's hum stops
	self->s.sound = 0;

	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		self->s.skinnum /= 2;
		ThrowGibs (self, damage, gladiator_gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	gi.sound (self, CHAN_BODY, sound_die, 1, ATTN_NORM, 0);
	if (rand() & 1)
		gi.sound (self, CHAN_VOICE, sound_die2, 1, ATTN_NORM, 0);

	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
	self->monsterinfo.currentmove = &gladiator_move_death;
}

static qboolean gladiator_blocked (edict_t *self, float dist)
{
	return blocked_checkplat (self, dist);
}

// monster_gladiator, and The Reckoning's monster_gladb
void SP_monster_gladiator_rr (edict_t *self)
{
	sound_pain1 = gi.soundindex ("gladiator/pain.wav");
	sound_pain2 = gi.soundindex ("gladiator/gldpain2.wav");
	sound_die = gi.soundindex ("gladiator/glddeth2.wav");
	sound_die2 = gi.soundindex ("gladiator/death.wav");
	sound_cleaver_swing = gi.soundindex ("gladiator/melee1.wav");
	sound_cleaver_hit = gi.soundindex ("gladiator/melee2.wav");
	sound_cleaver_miss = gi.soundindex ("gladiator/melee3.wav");
	sound_idle = gi.soundindex ("gladiator/gldidle1.wav");
	sound_search = gi.soundindex ("gladiator/gldsrch1.wav");
	sound_sight = gi.soundindex ("gladiator/sight.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/gladiatr/tris.md2");

	gi.modelindex ("models/monsters/gladiatr/gibs/chest.md2");
	gi.modelindex ("models/monsters/gladiatr/gibs/head.md2");
	gi.modelindex ("models/monsters/gladiatr/gibs/larm.md2");
	gi.modelindex ("models/monsters/gladiatr/gibs/rarm.md2");
	gi.modelindex ("models/monsters/gladiatr/gibs/thigh.md2");

	if (!strcmp (self->classname, "monster_gladb"))
	{
		sound_gunb = gi.soundindex ("weapons/plasshot.wav");

		self->health = 250;
		self->mass = 350;
		self->monsterinfo.power_armor_type = POWER_ARMOR_SHIELD;
		self->monsterinfo.power_armor_power = 250;
		self->s.skinnum = 2;
		self->style = 1;
		// his gun's hum, while he lives
		self->s.sound = gi.soundindex ("weapons/phaloop.wav");
	}
	else
	{
		sound_gun = gi.soundindex ("gladiator/railgun.wav");

		self->health = 400;
		self->mass = 400;
		self->s.sound = gi.soundindex ("weapons/rg_hum.wav");
	}

	self->gib_health = -175;

	VectorSet (self->mins, -32, -32, -24);
	VectorSet (self->maxs, 32, 32, 42);

	self->pain = gladiator_pain;
	self->die = gladiator_die;

	self->monsterinfo.stand = gladiator_stand;
	self->monsterinfo.walk = gladiator_walk;
	self->monsterinfo.run = gladiator_run;
	self->monsterinfo.dodge = NULL;
	self->monsterinfo.attack = gladiator_attack;
	self->monsterinfo.melee = gladiator_melee;
	self->monsterinfo.sight = gladiator_sight;
	self->monsterinfo.idle = gladiator_idle;
	self->monsterinfo.search = gladiator_search;
	self->monsterinfo.blocked = gladiator_blocked;
	self->monsterinfo.setskin = gladiator_setskin;

	gi.linkentity (self);
	self->monsterinfo.currentmove = &gladiator_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start (self);
}
