// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

SUPER TANK, the 2023 re-release's

Its m_supertank.cpp, in C, for its own maps (RR_Map): a super tank that
fires only with a clear shot, leads you with its rockets and chaingun,
lobs grenades judged to reach you (more when you are above it), won't
flinch from small hurts or while firing rockets, bursts with explosions
as it dies (longer on Quake 64's levels) and blows apart into pieces of
its own; and The Reckoning's, with a power shield and heat seekers. Its
frames are 1997's. m_supertank.c is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10. Its heat seekers turn
as The Reckoning's do.
==============================================================================
*/

#include "g_local.h"
#include "m_supertank.h"

qboolean blocked_checkplat (edict_t *self, float dist);
qboolean visible (edict_t *self, edict_t *other);

#define SPAWNFLAG_SUPERTANK_POWERSHIELD	8
#define SPAWNFLAG_SUPERTANK_LONG_DEATH	16

static int	sound_pain1;
static int	sound_pain2;
static int	sound_pain3;
static int	sound_death;
static int	sound_search1;
static int	sound_search2;

static int	tread_sound;

static void supertank_dead (edict_t *self);
static void supertankRocket (edict_t *self);
static void supertankMachineGun (edict_t *self);
static void supertank_reattack1 (edict_t *self);
static void supertank_run (edict_t *self);

static void TreadSound (edict_t *self)
{
	gi.sound (self, CHAN_BODY, tread_sound, 1, ATTN_NORM, 0);
}

static void supertank_search (edict_t *self)
{
	if (random() < 0.5f)
		gi.sound (self, CHAN_VOICE, sound_search1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_search2, 1, ATTN_NORM, 0);
}

static void BossExplode (edict_t *self)
{
	BossExplode_rr (self);
}

static mframe_t supertank_frames_stand[] = {
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
static mmove_t supertank_move_stand = { FRAME_stand_1, FRAME_stand_60, supertank_frames_stand, NULL };

static void supertank_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &supertank_move_stand;
}

static mframe_t supertank_frames_run[] = {
	{ ai_run, 12, TreadSound },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 },
	{ ai_run, 12 }
};
static mmove_t supertank_move_run = { FRAME_forwrd_1, FRAME_forwrd_18, supertank_frames_run, NULL };

static mframe_t supertank_frames_forward[] = {
	{ ai_walk, 4, TreadSound },
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
static mmove_t supertank_move_forward = { FRAME_forwrd_1, FRAME_forwrd_18, supertank_frames_forward, NULL };

static void supertank_forward (edict_t *self)
{
	self->monsterinfo.currentmove = &supertank_move_forward;
}

static void supertank_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &supertank_move_forward;
}

static void supertank_run (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &supertank_move_stand;
	else
		self->monsterinfo.currentmove = &supertank_move_run;
}

static mframe_t supertank_frames_pain3[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t supertank_move_pain3 = { FRAME_pain3_9, FRAME_pain3_12, supertank_frames_pain3, supertank_run };

static mframe_t supertank_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t supertank_move_pain2 = { FRAME_pain2_5, FRAME_pain2_8, supertank_frames_pain2, supertank_run };

static mframe_t supertank_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t supertank_move_pain1 = { FRAME_pain1_1, FRAME_pain1_4, supertank_frames_pain1, supertank_run };

static void BossLoop (edict_t *self)
{
	if (!(self->spawnflags & SPAWNFLAG_SUPERTANK_LONG_DEATH))
		return;

	if (self->count)
		self->count--;
	else
		self->spawnflags &= ~SPAWNFLAG_SUPERTANK_LONG_DEATH;

	self->monsterinfo.nextframe = FRAME_death_19;
}

static void supertankGrenade (edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	aim_point;
	int		flash_number;
	float	speed;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	if (self->s.frame == FRAME_attak4_1)
		flash_number = MZ2_SUPERTANK_GRENADE_1;
	else
		flash_number = MZ2_SUPERTANK_GRENADE_2;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);

	PredictAim (self, self->enemy, start, 0, false, crandom() * 0.1f, forward, aim_point);

	for (speed = 500.f; speed < 1000.f; speed += 100.f)
	{
		if (!M_CalculatePitchToFire (self, aim_point, start, forward, speed, 2.5f, true, false))
			continue;

		monster_fire_grenade (self, start, forward, 50, speed, flash_number);
		break;
	}
}

static mframe_t supertank_frames_death1[] = {
	{ ai_move, 0, BossExplode },
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
	{ ai_move, 0, BossLoop }
};
static mmove_t supertank_move_death = { FRAME_death_1, FRAME_death_24, supertank_frames_death1, supertank_dead };

static mframe_t supertank_frames_attack4[] = {
	{ ai_move, 0, supertankGrenade },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, supertankGrenade },
	{ ai_move },
	{ ai_move }
};
static mmove_t supertank_move_attack4 = { FRAME_attak4_1, FRAME_attak4_6, supertank_frames_attack4, supertank_run };

static mframe_t supertank_frames_attack2[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, supertankRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t supertank_move_attack2 = { FRAME_attak2_1, FRAME_attak2_27, supertank_frames_attack2, supertank_run };

static mframe_t supertank_frames_attack1[] = {
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
	{ ai_charge, 0, supertankMachineGun },
};
static mmove_t supertank_move_attack1 = { FRAME_attak1_1, FRAME_attak1_6, supertank_frames_attack1, supertank_reattack1 };

static mframe_t supertank_frames_end_attack1[] = {
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
static mmove_t supertank_move_end_attack1 = { FRAME_attak1_7, FRAME_attak1_20, supertank_frames_end_attack1, supertank_run };

static void supertank_reattack1 (edict_t *self)
{
	if (visible (self, self->enemy))
	{
		if (self->timestamp >= level.time || random() < 0.3f)
			self->monsterinfo.currentmove = &supertank_move_attack1;
		else
			self->monsterinfo.currentmove = &supertank_move_end_attack1;
	}
	else
		self->monsterinfo.currentmove = &supertank_move_end_attack1;
}

static void supertank_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (level.time < self->pain_debounce_time)
		return;

	// Lessen the chance of him going into his pain frames
	if (meansOfDeath != MOD_CHAINFIST)
	{
		if (damage <= 25)
			if (random() < 0.2f)
				return;

		// Don't go into pain if he's firing his rockets
		if ((self->s.frame >= FRAME_attak2_1) && (self->s.frame <= FRAME_attak2_14))
			return;
	}

	if (damage <= 10)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else if (damage <= 25)
		gi.sound (self, CHAN_VOICE, sound_pain3, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	self->pain_debounce_time = level.time + 3;

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	if (damage <= 10)
		self->monsterinfo.currentmove = &supertank_move_pain1;
	else if (damage <= 25)
		self->monsterinfo.currentmove = &supertank_move_pain2;
	else
		self->monsterinfo.currentmove = &supertank_move_pain3;
}

static void supertank_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

static void supertankRocket (edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	dir;
	vec3_t	vec;
	int		flash_number;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	if (self->s.frame == FRAME_attak2_8)
		flash_number = MZ2_SUPERTANK_ROCKET_1;
	else if (self->s.frame == FRAME_attak2_11)
		flash_number = MZ2_SUPERTANK_ROCKET_2;
	else // (self->s.frame == FRAME_attak2_14)
		flash_number = MZ2_SUPERTANK_ROCKET_3;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);

	if (self->spawnflags & SPAWNFLAG_SUPERTANK_POWERSHIELD)
	{
		VectorCopy (self->enemy->s.origin, vec);
		vec[2] += self->enemy->viewheight;
		VectorSubtract (vec, start, dir);
		VectorNormalize (dir);
		monster_fire_heat (self, start, dir, 40, 500, flash_number);
	}
	else
	{
		PredictAim (self, self->enemy, start, 750, false, 0.f, forward, NULL);
		monster_fire_rocket (self, start, forward, 50, 750, flash_number);
	}
}

static void supertankMachineGun (edict_t *self)
{
	vec3_t	dir;
	vec3_t	start;
	vec3_t	forward, right;
	int		flash_number;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	flash_number = MZ2_SUPERTANK_MACHINEGUN_1 + (self->s.frame - FRAME_attak1_1);

	dir[0] = 0;
	dir[1] = self->s.angles[1];
	dir[2] = 0;

	AngleVectors (dir, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);
	PredictAim (self, self->enemy, start, 0, true, -0.1f, forward, NULL);

	monster_fire_bullet (self, start, forward, 6, 4, DEFAULT_BULLET_HSPREAD * 3, DEFAULT_BULLET_VSPREAD * 3, flash_number);
}

static void supertank_attack (edict_t *self)
{
	vec3_t		vec, clear;
	float		range;
	qboolean	chaingun_good, rocket_good, grenade_good;

	VectorSubtract (self->enemy->s.origin, self->s.origin, vec);
	range = range_to (self, self->enemy);

	// Attack 1 == Chaingun
	// Attack 2 == Rocket Launcher
	// Attack 3 == Grenade Launcher
	chaingun_good = M_CheckClearShot (self, monster_flash_offset[MZ2_SUPERTANK_MACHINEGUN_1], clear);
	rocket_good = M_CheckClearShot (self, monster_flash_offset[MZ2_SUPERTANK_ROCKET_1], clear);
	grenade_good = M_CheckClearShot (self, monster_flash_offset[MZ2_SUPERTANK_GRENADE_1], clear);

	// fire rockets more often at distance
	if (chaingun_good && (!rocket_good || range <= 540 || random() < 0.3f))
	{
		// prefer grenade if the enemy is above us
		if (grenade_good && (range >= 350 || vec[2] > 120.f || random() < 0.2f))
			self->monsterinfo.currentmove = &supertank_move_attack4;
		else
		{
			self->monsterinfo.currentmove = &supertank_move_attack1;
			self->timestamp = level.time + 1.5 + 1.2 * random();
		}
	}
	else if (rocket_good)
	{
		// prefer grenade if the enemy is above us
		if (grenade_good && (vec[2] > 120.f || random() < 0.2f))
			self->monsterinfo.currentmove = &supertank_move_attack4;
		else
			self->monsterinfo.currentmove = &supertank_move_attack2;
	}
	else if (grenade_good)
		self->monsterinfo.currentmove = &supertank_move_attack4;
}

static gib_def_t supertank_gibs[] = {
	{ 2, "models/objects/gibs/sm_meat/tris.md2" },
	{ 2, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
	{ 1, "models/monsters/boss1/gibs/cgun.md2", GIB_SKINNED | GIB_METALLIC },
	{ 1, "models/monsters/boss1/gibs/chest.md2", GIB_SKINNED },
	{ 1, "models/monsters/boss1/gibs/core.md2", GIB_SKINNED },
	{ 1, "models/monsters/boss1/gibs/ltread.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/boss1/gibs/rgun.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/boss1/gibs/rtread.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/boss1/gibs/tube.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/boss1/gibs/head.md2", GIB_SKINNED | GIB_METALLIC | GIB_HEAD },
	{ 0, NULL }
};

static void supertank_gib (edict_t *self)
{
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1_BIG);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	self->s.sound = 0;
	self->s.skinnum /= 2;

	ThrowGibs (self, 500, supertank_gibs);
}

static void supertank_dead (edict_t *self)
{
	supertank_gib (self);
}

static void supertank_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (self->deadflag == DEAD_DEAD)
		return;

	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_NO;

	self->monsterinfo.currentmove = &supertank_move_death;
}

static qboolean supertank_blocked (edict_t *self, float dist)
{
	return blocked_checkplat (self, dist);
}

void SP_monster_supertank_rr (edict_t *self)
{
	sound_pain1 = gi.soundindex ("bosstank/btkpain1.wav");
	sound_pain2 = gi.soundindex ("bosstank/btkpain2.wav");
	sound_pain3 = gi.soundindex ("bosstank/btkpain3.wav");
	sound_death = gi.soundindex ("bosstank/btkdeth1.wav");
	sound_search1 = gi.soundindex ("bosstank/btkunqv1.wav");
	sound_search2 = gi.soundindex ("bosstank/btkunqv2.wav");

	tread_sound = gi.soundindex ("bosstank/btkengn1.wav");

	gi.soundindex ("gunner/gunatck3.wav");
	gi.soundindex ("infantry/infatck1.wav");
	gi.soundindex ("tank/rocket.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/boss1/tris.md2");

	gi.modelindex ("models/monsters/boss1/gibs/cgun.md2");
	gi.modelindex ("models/monsters/boss1/gibs/chest.md2");
	gi.modelindex ("models/monsters/boss1/gibs/core.md2");
	gi.modelindex ("models/monsters/boss1/gibs/head.md2");
	gi.modelindex ("models/monsters/boss1/gibs/ltread.md2");
	gi.modelindex ("models/monsters/boss1/gibs/rgun.md2");
	gi.modelindex ("models/monsters/boss1/gibs/rtread.md2");
	gi.modelindex ("models/monsters/boss1/gibs/tube.md2");

	VectorSet (self->mins, -64, -64, 0);
	VectorSet (self->maxs, 64, 64, 112);

	self->health = 1500;
	self->gib_health = -500;
	self->mass = 800;

	self->pain = supertank_pain;
	self->die = supertank_die;
	self->monsterinfo.stand = supertank_stand;
	self->monsterinfo.walk = supertank_walk;
	self->monsterinfo.run = supertank_run;
	self->monsterinfo.dodge = NULL;
	self->monsterinfo.attack = supertank_attack;
	self->monsterinfo.search = supertank_search;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = NULL;
	self->monsterinfo.blocked = supertank_blocked; // PGM
	self->monsterinfo.setskin = supertank_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &supertank_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	// RAFAEL: The Reckoning's
	if (!strcmp (self->classname, "monster_boss5"))
		self->spawnflags |= SPAWNFLAG_SUPERTANK_POWERSHIELD;
	if (self->spawnflags & SPAWNFLAG_SUPERTANK_POWERSHIELD)
	{
		self->monsterinfo.power_armor_type = POWER_ARMOR_SHIELD;
		self->monsterinfo.power_armor_power = 400;
	}
	// RAFAEL

	walkmonster_start (self);

	// PMM
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	// pmm

	if (level.is_n64)
	{
		self->spawnflags |= SPAWNFLAG_SUPERTANK_LONG_DEATH;
		self->count = 10;
	}

	if (!strcmp (self->classname, "monster_boss5"))
	{
		gi.soundindex ("weapons/railgr1a.wav");
		self->s.skinnum = 2;
	}
}
