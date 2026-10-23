// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

SHAMBLER

The re-release's shambler (monster_shambler), ported to the 1997 API.
Call of the Machine's mgu1m4, mgu6m2, mgu6m3 and mguboss have them.

==============================================================================
*/

#include "g_local.h"
#include "m_shambler.h"

static int	sound_pain;
static int	sound_idle;
static int	sound_die;
static int	sound_sight;
static int	sound_windup;
static int	sound_melee1;
static int	sound_melee2;
static int	sound_smack;
static int	sound_boom;

#define	SPAWNFLAG_SHAMBLER_PRECISE	1

//
// misc
//

void shambler_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static vec3_t lightning_left_hand[] = {
	{ 44, 36, 25 },
	{ 10, 44, 57 },
	{ -1, 40, 70 },
	{ -10, 34, 75 },
	{ 7.4, 24, 89 }
};

static vec3_t lightning_right_hand[] = {
	{ 28, -38, 25 },
	{ 31, -7, 70 },
	{ 20, 0, 80 },
	{ 16, 1.2, 81 },
	{ 27, -11, 83 }
};

#define	NUM_LIGHTNING_FRAMES	(sizeof(lightning_left_hand) / sizeof(lightning_left_hand[0]))

// the lightning between its hands as it winds up
static void shambler_lightning_update (edict_t *self)
{
	edict_t	*lightning = self->beam;
	vec3_t	f, r;

	if (!lightning)
		return;

	if (self->s.frame >= FRAME_magic01 + NUM_LIGHTNING_FRAMES)
	{
		G_FreeEdict (lightning);
		self->beam = NULL;
		return;
	}

	AngleVectors (self->s.angles, f, r, NULL);
	G_ProjectSource (self->s.origin, lightning_left_hand[self->s.frame - FRAME_magic01], f, r, lightning->s.origin);
	G_ProjectSource (self->s.origin, lightning_right_hand[self->s.frame - FRAME_magic01], f, r, lightning->s.old_origin);
	gi.linkentity (lightning);
}

void shambler_windup (edict_t *self)
{
	edict_t	*lightning;

	gi.sound (self, CHAN_WEAPON, sound_windup, 1, ATTN_NORM, 0);

	if (self->beam)
		G_FreeEdict (self->beam);
	lightning = self->beam = G_Spawn ();
	lightning->classname = "shambler_lightning";
	lightning->s.modelindex = gi.modelindex ("models/proj/lightning/tris.md2");
	lightning->s.renderfx |= RF_BEAM;
	lightning->owner = self;
	shambler_lightning_update (self);
}

void shambler_idle (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

void shambler_maybe_idle (edict_t *self)
{
	if (random() > 0.8)
		gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

//
// stand
//

mframe_t shambler_frames_stand [] =
{
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL
};
mmove_t shambler_move_stand = {FRAME_stand01, FRAME_stand17, shambler_frames_stand, NULL};

void shambler_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &shambler_move_stand;
}

//
// walk
//

mframe_t shambler_frames_walk [] =
{
	ai_walk, 10, NULL,
	ai_walk, 9, NULL,
	ai_walk, 9, NULL,
	ai_walk, 5, NULL,
	ai_walk, 6, NULL,
	ai_walk, 12, NULL,
	ai_walk, 8, NULL,
	ai_walk, 3, NULL,
	ai_walk, 13, NULL,
	ai_walk, 9, NULL,
	ai_walk, 7, shambler_maybe_idle,
	ai_walk, 5, NULL
};
mmove_t shambler_move_walk = {FRAME_walk01, FRAME_walk12, shambler_frames_walk, NULL};

void shambler_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &shambler_move_walk;
}

//
// run
//

mframe_t shambler_frames_run [] =
{
	ai_run, 20, NULL,
	ai_run, 24, NULL,
	ai_run, 20, NULL,
	ai_run, 20, NULL,
	ai_run, 24, NULL,
	ai_run, 20, shambler_maybe_idle
};
mmove_t shambler_move_run = {FRAME_run01, FRAME_run06, shambler_frames_run, NULL};

void shambler_run (edict_t *self)
{
	if (self->enemy && self->enemy->client)
		self->monsterinfo.aiflags |= AI_BRUTAL;
	else
		self->monsterinfo.aiflags &= ~AI_BRUTAL;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.currentmove = &shambler_move_stand;
		return;
	}

	self->monsterinfo.currentmove = &shambler_move_run;
}

//
// pain
//

mframe_t shambler_frames_pain [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t shambler_move_pain = {FRAME_pain01, FRAME_pain06, shambler_frames_pain, shambler_run};

void shambler_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (level.time < self->timestamp)
		return;

	self->timestamp = level.time + 0.001;
	gi.sound (self, CHAN_AUTO, sound_pain, 1, ATTN_NORM, 0);

	if (meansOfDeath != MOD_CHAINFIST && damage <= 30 && random() > 0.2)
		return;

	// on hard or nightmare, no pain while attacking
	if (skill->value >= 2)
	{
		if (self->s.frame >= FRAME_smash01 && self->s.frame <= FRAME_smash12)
			return;
		if (self->s.frame >= FRAME_swingl01 && self->s.frame <= FRAME_swingl09)
			return;
		if (self->s.frame >= FRAME_swingr01 && self->s.frame <= FRAME_swingr09)
			return;
	}

	if (skill->value == 3)
		return;		// no pain anims in nightmare

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 2;
	self->monsterinfo.currentmove = &shambler_move_pain;

	// a windup it was in is over: its lightning with it
	if (self->beam)
	{
		G_FreeEdict (self->beam);
		self->beam = NULL;
	}
}

//
// attacks
//

void ShamblerSaveLoc (edict_t *self)
{
	VectorCopy (self->enemy->s.origin, self->pos1);		// for aiming the shot
	self->pos1[2] += self->enemy->viewheight;
	self->monsterinfo.nextframe = FRAME_magic09;

	gi.sound (self, CHAN_WEAPON, sound_boom, 1, ATTN_NORM, 0);
	shambler_lightning_update (self);
}

// the highest of its offsets with a clear shot
static void FindShamblerOffset (edict_t *self, vec3_t offset)
{
	vec3_t	start;
	int		i;

	VectorSet (offset, 0, 0, 48);
	for (i = 0; i < 8; i++)
	{
		if (M_CheckClearShot (self, offset, start))
			return;
		offset[2] -= 4;
	}
	VectorSet (offset, 0, 0, 48);
}

void ShamblerCastLightning (edict_t *self)
{
	vec3_t	start, dir, forward, right, offset, end;
	trace_t	tr;

	if (!self->enemy)
		return;

	FindShamblerOffset (self, offset);

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, offset, forward, right, start);

	// at where it aimed
	PredictAim (self, self->enemy, start, 0, false,
		(self->spawnflags & SPAWNFLAG_SHAMBLER_PRECISE) ? 0 : 0.1, dir, NULL);

	VectorMA (start, 8192, dir, end);
	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_LIGHTNING);
	gi.WriteShort (self - g_edicts);	// source entity
	gi.WriteShort (0);					// destination entity: the world
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (start, MULTICAST_PVS);

	fire_bullet (self, start, dir, 8 + rand() % 4, 15, 0, 0, MOD_TESLA);
}

mframe_t shambler_frames_magic [] =
{
	ai_charge, 0, shambler_windup,
	ai_charge, 0, shambler_lightning_update,
	ai_charge, 0, shambler_lightning_update,
	ai_move, 0, shambler_lightning_update,
	ai_move, 0, shambler_lightning_update,
	ai_move, 0, ShamblerSaveLoc,
	ai_move, 0, NULL,
	ai_charge, 0, NULL,
	ai_move, 0, ShamblerCastLightning,
	ai_move, 0, ShamblerCastLightning,
	ai_move, 0, ShamblerCastLightning,
	ai_move, 0, NULL
};
mmove_t shambler_attack_magic = {FRAME_magic01, FRAME_magic12, shambler_frames_magic, shambler_run};

void shambler_attack (edict_t *self)
{
	self->monsterinfo.currentmove = &shambler_attack_magic;
}

//
// melee
//

void shambler_melee1 (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_melee1, 1, ATTN_NORM, 0);
}

void shambler_melee2 (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_melee2, 1, ATTN_NORM, 0);
}

void sham_swingl9 (edict_t *self);
void sham_swingr9 (edict_t *self);

void sham_smash10 (edict_t *self)
{
	vec3_t	aim;

	if (!self->enemy)
		return;

	ai_charge (self, 0);

	if (!CanDamage (self->enemy, self))
		return;

	VectorSet (aim, MELEE_DISTANCE_RR, self->mins[0], -4);
	if (fire_hit_rr (self, aim, 110 + rand() % 10, 120))		// slower attack
		gi.sound (self, CHAN_WEAPON, sound_smack, 1, ATTN_NORM, 0);
}

void ShamClaw (edict_t *self)
{
	vec3_t	aim;

	if (!self->enemy)
		return;

	ai_charge (self, 10);

	if (!CanDamage (self->enemy, self))
		return;

	VectorSet (aim, MELEE_DISTANCE_RR, self->mins[0], -4);
	if (fire_hit_rr (self, aim, 70 + rand() % 10, 80))		// slower attack
		gi.sound (self, CHAN_WEAPON, sound_smack, 1, ATTN_NORM, 0);
}

mframe_t shambler_frames_smash [] =
{
	ai_charge, 2, shambler_melee1,
	ai_charge, 6, NULL,
	ai_charge, 6, NULL,
	ai_charge, 5, NULL,
	ai_charge, 4, NULL,
	ai_charge, 1, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, NULL,
	ai_charge, 0, sham_smash10,
	ai_charge, 5, NULL,
	ai_charge, 4, NULL
};
mmove_t shambler_attack_smash = {FRAME_smash01, FRAME_smash12, shambler_frames_smash, shambler_run};

mframe_t shambler_frames_swingl [] =
{
	ai_charge, 5, shambler_melee1,
	ai_charge, 3, NULL,
	ai_charge, 7, NULL,
	ai_charge, 3, NULL,
	ai_charge, 7, NULL,
	ai_charge, 9, NULL,
	ai_charge, 5, ShamClaw,
	ai_charge, 4, NULL,
	ai_charge, 8, sham_swingl9
};
mmove_t shambler_attack_swingl = {FRAME_swingl01, FRAME_swingl09, shambler_frames_swingl, shambler_run};

mframe_t shambler_frames_swingr [] =
{
	ai_charge, 1, shambler_melee2,
	ai_charge, 8, NULL,
	ai_charge, 14, NULL,
	ai_charge, 7, NULL,
	ai_charge, 3, NULL,
	ai_charge, 6, NULL,
	ai_charge, 6, ShamClaw,
	ai_charge, 3, NULL,
	ai_charge, 8, sham_swingr9
};
mmove_t shambler_attack_swingr = {FRAME_swingr01, FRAME_swingr09, shambler_frames_swingr, shambler_run};

void sham_swingl9 (edict_t *self)
{
	ai_charge (self, 8);

	if ((rand() & 1) && self->enemy && range_to (self, self->enemy) < MELEE_DISTANCE_RR)
		self->monsterinfo.currentmove = &shambler_attack_swingr;
}

void sham_swingr9 (edict_t *self)
{
	ai_charge (self, 1);
	ai_charge (self, 10);

	if ((rand() & 1) && self->enemy && range_to (self, self->enemy) < MELEE_DISTANCE_RR)
		self->monsterinfo.currentmove = &shambler_attack_swingl;
}

void shambler_melee (edict_t *self)
{
	float	chance = random();

	if (chance > 0.6 || self->health == 600)
		self->monsterinfo.currentmove = &shambler_attack_smash;
	else if (chance > 0.3)
		self->monsterinfo.currentmove = &shambler_attack_swingl;
	else
		self->monsterinfo.currentmove = &shambler_attack_swingr;
}

//
// death
//

void shambler_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, 0);
	monster_dead (self);
}

static void shambler_shrink (edict_t *self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

mframe_t shambler_frames_death [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, shambler_shrink,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t shambler_move_death = {FRAME_death01, FRAME_death11, shambler_frames_death, shambler_dead};

void shambler_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (self->beam)
	{
		G_FreeEdict (self->beam);
		self->beam = NULL;
	}

	// check for gib
	if (self->health <= self->gib_health)
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		ThrowGib (self, "models/objects/gibs/chest/tris.md2", damage, GIB_ORGANIC);
		ThrowHead (self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	gi.sound (self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	self->monsterinfo.currentmove = &shambler_move_death;
}

/*QUAKED monster_shambler (1 .5 0) (-32 -32 -24) (32 32 64) Ambush Trigger_Spawn Sight
*/
void SP_monster_shambler (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}

	self->s.modelindex = gi.modelindex ("models/monsters/shambler/tris.md2");
	VectorSet (self->mins, -32, -32, -24);
	VectorSet (self->maxs, 32, 32, 64);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	gi.modelindex ("models/proj/lightning/tris.md2");
	sound_pain = gi.soundindex ("shambler/shurt2.wav");
	sound_idle = gi.soundindex ("shambler/sidle.wav");
	sound_die = gi.soundindex ("shambler/sdeath.wav");
	sound_windup = gi.soundindex ("shambler/sattck1.wav");
	sound_melee1 = gi.soundindex ("shambler/melee1.wav");
	sound_melee2 = gi.soundindex ("shambler/melee2.wav");
	sound_sight = gi.soundindex ("shambler/ssight.wav");
	sound_smack = gi.soundindex ("shambler/smack.wav");
	sound_boom = gi.soundindex ("shambler/sboom.wav");

	self->health = 600;		// monster_start applies its health_multiplier
	self->gib_health = -60;

	self->mass = 500;

	self->pain = shambler_pain;
	self->die = shambler_die;
	self->monsterinfo.stand = shambler_stand;
	self->monsterinfo.walk = shambler_walk;
	self->monsterinfo.run = shambler_run;
	self->monsterinfo.dodge = NULL;
	self->monsterinfo.attack = shambler_attack;
	self->monsterinfo.melee = shambler_melee;
	self->monsterinfo.sight = shambler_sight;
	self->monsterinfo.idle = shambler_idle;

	gi.linkentity (self);

	if (self->spawnflags & SPAWNFLAG_SHAMBLER_PRECISE)
		self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	self->monsterinfo.currentmove = &shambler_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start (self);
}
