// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

HORNET, the 2023 re-release's

Its m_boss2.cpp, in C, for its own maps (RR_Map): a hornet whose
rockets mostly lead you, spread across your path, whose chainguns lead
you too, and that keeps firing while you are in front of it; Quake 64's
(its Hyperblaster spawnflag) fires hyperblasters and a stream of single
rockets; laser-proof, it sinks slowly as it dies, bursting with
explosions, and blows apart into pieces of its own. Its frames are
1997's. m_boss2.c is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10. Its gibs drawn larger
there are drawn at their own size here, as nothing here is drawn scaled.
==============================================================================
*/

#include "g_local.h"
#include "m_boss2.h"

qboolean infront (edict_t *self, edict_t *other);
qboolean Boss2_CheckAttack (edict_t *self);	// 1997's, its chances the same

#define SPAWNFLAG_BOSS2_N64		8
#define BOSS2_ROCKET_SPEED		750

static int	sound_pain1;
static int	sound_pain2;
static int	sound_pain3;
static int	sound_death;
static int	sound_search1;

static void boss2_run (edict_t *self);
static void boss2_dead (edict_t *self);
static void boss2_attack_mg (edict_t *self);
static void boss2_reattack_mg (edict_t *self);
static void boss2_shrink (edict_t *self);
static void Boss2HyperBlaster (edict_t *self);

static void boss2_search (edict_t *self)
{
	if (random() < 0.5f)
		gi.sound (self, CHAN_VOICE, sound_search1, 1, ATTN_NONE, 0);
}

static void BossExplode (edict_t *self)
{
	BossExplode_rr (self);
}

static void boss2_hb_reattack (edict_t *self)
{
	Boss2HyperBlaster (self);
	boss2_reattack_mg (self);
}

// one of its four rockets, at where the enemy will be, less lead seconds
static void Boss2PredictiveRocket1 (edict_t *self, vec3_t forward, vec3_t right, int flash, float lead)
{
	vec3_t	start, dir;

	G_ProjectSource (self->s.origin, monster_flash_offset[flash], forward, right, start);
	PredictAim (self, self->enemy, start, BOSS2_ROCKET_SPEED, false, lead, dir, NULL);
	monster_fire_rocket (self, start, dir, 50, BOSS2_ROCKET_SPEED, flash);
}

static void Boss2PredictiveRocket (edict_t *self)
{
	vec3_t	forward, right;

	AngleVectors (self->s.angles, forward, right, NULL);

	Boss2PredictiveRocket1 (self, forward, right, MZ2_BOSS2_ROCKET_1, -0.10f);
	Boss2PredictiveRocket1 (self, forward, right, MZ2_BOSS2_ROCKET_2, -0.05f);
	Boss2PredictiveRocket1 (self, forward, right, MZ2_BOSS2_ROCKET_3, 0.05f);
	Boss2PredictiveRocket1 (self, forward, right, MZ2_BOSS2_ROCKET_4, 0.10f);
}

// one of its four rockets, spread to the side by spread, lower by drop
static void Boss2Rocket1 (edict_t *self, vec3_t forward, vec3_t right, int flash, float spread, float drop)
{
	vec3_t	start, dir, vec;

	G_ProjectSource (self->s.origin, monster_flash_offset[flash], forward, right, start);
	VectorCopy (self->enemy->s.origin, vec);
	vec[2] -= drop;
	VectorSubtract (vec, start, dir);
	VectorNormalize (dir);
	VectorMA (dir, spread, right, dir);
	VectorNormalize (dir);
	monster_fire_rocket (self, start, dir, 50, 500, flash);
}

static void Boss2Rocket (edict_t *self)
{
	vec3_t	forward, right;

	if (self->enemy)
	{
		if (self->enemy->client && random() < 0.9f)
		{
			Boss2PredictiveRocket (self);
			return;
		}
	}

	AngleVectors (self->s.angles, forward, right, NULL);

	Boss2Rocket1 (self, forward, right, MZ2_BOSS2_ROCKET_1, 0.4f, 15);
	Boss2Rocket1 (self, forward, right, MZ2_BOSS2_ROCKET_2, 0.025f, 0);
	Boss2Rocket1 (self, forward, right, MZ2_BOSS2_ROCKET_3, -0.025f, 0);
	Boss2Rocket1 (self, forward, right, MZ2_BOSS2_ROCKET_4, -0.4f, 15);
}

static void Boss2Rocket64 (edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	dir;
	vec3_t	vec;
	float	time, dist;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_BOSS2_ROCKET_1], forward, right, start);

	start[2] += 10.f;
	VectorMA (start, -2.f, right, start);
	VectorMA (start, -((self->count++ % 4) * 8.f), right, start);

	if (self->enemy && self->enemy->client && random() < 0.9f)
	{
		// 1
		VectorSubtract (self->enemy->s.origin, start, dir);
		dist = VectorLength (dir);
		time = dist / BOSS2_ROCKET_SPEED;
		VectorMA (self->enemy->s.origin, time - 0.3f, self->enemy->velocity, vec);
	}
	else
	{
		// 1
		VectorCopy (self->enemy->s.origin, vec);
		vec[2] -= 15;
	}

	VectorSubtract (vec, start, dir);
	VectorNormalize (dir);

	monster_fire_rocket (self, start, dir, 35, BOSS2_ROCKET_SPEED, MZ2_BOSS2_ROCKET_1);
}

static void boss2_firebullet (edict_t *self, int flash)
{
	vec3_t	forward, right, start;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash], forward, right, start);
	PredictAim (self, self->enemy, start, 0, true, -0.2f, forward, NULL);
	monster_fire_bullet (self, start, forward, 6, 4, DEFAULT_BULLET_HSPREAD * 3, DEFAULT_BULLET_VSPREAD, flash);
}

static void Boss2MachineGun (edict_t *self)
{
	boss2_firebullet (self, MZ2_BOSS2_MACHINEGUN_L1);
	boss2_firebullet (self, MZ2_BOSS2_MACHINEGUN_R1);
}

static mframe_t boss2_frames_stand[] = {
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
static mmove_t boss2_move_stand = { FRAME_stand30, FRAME_stand50, boss2_frames_stand, NULL };

static mframe_t boss2_frames_walk[] = {
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 },
	{ ai_walk, 10 }
};
static mmove_t boss2_move_walk = { FRAME_walk1, FRAME_walk20, boss2_frames_walk, NULL };

static mframe_t boss2_frames_run[] = {
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
static mmove_t boss2_move_run = { FRAME_walk1, FRAME_walk20, boss2_frames_run, NULL };

static mframe_t boss2_frames_attack_pre_mg[] = {
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2, boss2_attack_mg }
};
static mmove_t boss2_move_attack_pre_mg = { FRAME_attack1, FRAME_attack9, boss2_frames_attack_pre_mg, NULL };

static mframe_t boss2_frames_attack_mg[] = {
	{ ai_charge, 2, Boss2MachineGun },
	{ ai_charge, 2, Boss2MachineGun },
	{ ai_charge, 2, Boss2MachineGun },
	{ ai_charge, 2, Boss2MachineGun },
	{ ai_charge, 2, Boss2MachineGun },
	{ ai_charge, 2, boss2_reattack_mg }
};
static mmove_t boss2_move_attack_mg = { FRAME_attack10, FRAME_attack15, boss2_frames_attack_mg, NULL };

static void Boss2HyperBlaster (edict_t *self)
{
	vec3_t	forward, right, target;
	vec3_t	start;
	int		id = (self->s.frame & 1) ? MZ2_BOSS2_MACHINEGUN_L2 : MZ2_BOSS2_MACHINEGUN_R2;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[id], forward, right, start);
	VectorCopy (self->enemy->s.origin, target);
	target[2] += self->enemy->viewheight;
	VectorSubtract (target, start, forward);
	VectorNormalize (forward);

	monster_fire_blaster (self, start, forward, 2, 1000, id, (self->s.frame % 4) ? 0 : EF_HYPERBLASTER);
}

static mframe_t boss2_frames_attack_hb[] = {
	{ ai_charge, 2, Boss2HyperBlaster },
	{ ai_charge, 2, Boss2HyperBlaster },
	{ ai_charge, 2, Boss2HyperBlaster },
	{ ai_charge, 2, Boss2HyperBlaster },
	{ ai_charge, 2, Boss2HyperBlaster },
	{ ai_charge, 2, boss2_hb_reattack }
};
static mmove_t boss2_move_attack_hb = { FRAME_attack10, FRAME_attack15, boss2_frames_attack_hb, NULL };

static mframe_t boss2_frames_attack_post_mg[] = {
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 }
};
static mmove_t boss2_move_attack_post_mg = { FRAME_attack16, FRAME_attack19, boss2_frames_attack_post_mg, boss2_run };

static mframe_t boss2_frames_attack_rocket[] = {
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_move, -5, Boss2Rocket },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 }
};
static mmove_t boss2_move_attack_rocket = { FRAME_attack20, FRAME_attack40, boss2_frames_attack_rocket, boss2_run };

static mframe_t boss2_frames_attack_rocket2[] = {
	{ ai_charge, 2, Boss2Rocket64 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2, Boss2Rocket64 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2, Boss2Rocket64 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2, Boss2Rocket64 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2, Boss2Rocket64 },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2 }
};
static mmove_t boss2_move_attack_rocket2 = { FRAME_attack20, FRAME_attack39, boss2_frames_attack_rocket2, boss2_run };

static mframe_t boss2_frames_pain_heavy[] = {
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
static mmove_t boss2_move_pain_heavy = { FRAME_pain2, FRAME_pain19, boss2_frames_pain_heavy, boss2_run };

static mframe_t boss2_frames_pain_light[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t boss2_move_pain_light = { FRAME_pain20, FRAME_pain23, boss2_frames_pain_light, boss2_run };

static void boss2_shrink (edict_t *self)
{
	self->maxs[2] = 50.f;
	gi.linkentity (self);
}

static mframe_t boss2_frames_death[] = {
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
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, boss2_shrink },
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
static mmove_t boss2_move_death = { FRAME_death2, FRAME_death50, boss2_frames_death, boss2_dead };

static void boss2_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &boss2_move_stand;
}

static void boss2_run (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &boss2_move_stand;
	else
		self->monsterinfo.currentmove = &boss2_move_run;
}

static void boss2_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &boss2_move_walk;
}

static void boss2_attack (edict_t *self)
{
	vec3_t	vec;
	float	range;

	VectorSubtract (self->enemy->s.origin, self->s.origin, vec);
	range = VectorLength (vec);

	if (range <= 125 || random() <= 0.6f)
		self->monsterinfo.currentmove = (self->spawnflags & SPAWNFLAG_BOSS2_N64) ? &boss2_move_attack_hb : &boss2_move_attack_pre_mg;
	else
		self->monsterinfo.currentmove = (self->spawnflags & SPAWNFLAG_BOSS2_N64) ? &boss2_move_attack_rocket2 : &boss2_move_attack_rocket;
}

static void boss2_attack_mg (edict_t *self)
{
	self->monsterinfo.currentmove = (self->spawnflags & SPAWNFLAG_BOSS2_N64) ? &boss2_move_attack_hb : &boss2_move_attack_mg;
}

static void boss2_reattack_mg (edict_t *self)
{
	if (infront (self, self->enemy) && random() <= 0.7f)
		boss2_attack_mg (self);
	else
		self->monsterinfo.currentmove = &boss2_move_attack_post_mg;
}

static void boss2_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	// American wanted these at no attenuation
	if (damage < 10)
		gi.sound (self, CHAN_VOICE, sound_pain3, 1, ATTN_NONE, 0);
	else if (damage < 30)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NONE, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NONE, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	if (damage < 30)
		self->monsterinfo.currentmove = &boss2_move_pain_light;
	else
		self->monsterinfo.currentmove = &boss2_move_pain_heavy;
}

static void boss2_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static gib_def_t boss2_gibs[] = {
	{ 2, "models/objects/gibs/sm_meat/tris.md2" },
	{ 2, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
	{ 1, "models/monsters/boss2/gibs/chest.md2", GIB_SKINNED },
	{ 2, "models/monsters/boss2/gibs/chaingun.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/boss2/gibs/cpu.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/boss2/gibs/engine.md2", GIB_SKINNED },
	{ 1, "models/monsters/boss2/gibs/rocket.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/boss2/gibs/spine.md2", GIB_SKINNED },
	{ 2, "models/monsters/boss2/gibs/wing.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 3, "models/monsters/boss2/gibs/larm.md2", GIB_SKINNED | GIB_UPRIGHT },	// two of them larger there
	{ 3, "models/monsters/boss2/gibs/rarm.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/boss2/gibs/head.md2", GIB_SKINNED | GIB_METALLIC | GIB_HEAD },
	{ 0, NULL }
};

static void boss2_gib (edict_t *self)
{
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1_BIG);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	self->s.sound = 0;
	self->s.skinnum /= 2;
	self->gravity = 1.0f;

	ThrowGibs (self, 500, boss2_gibs);
}

static void boss2_dead (edict_t *self)
{
	boss2_gib (self);
}

static void boss2_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (self->deadflag == DEAD_DEAD)
		return;

	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NONE, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_NO;
	self->count = 0;
	VectorClear (self->velocity);
	// it sinks slowly, its engine off
	self->gravity *= 0.30f;
	self->s.sound = 0;

	self->monsterinfo.currentmove = &boss2_move_death;
}

void SP_monster_boss2_rr (edict_t *self)
{
	sound_pain1 = gi.soundindex ("bosshovr/bhvpain1.wav");
	sound_pain2 = gi.soundindex ("bosshovr/bhvpain2.wav");
	sound_pain3 = gi.soundindex ("bosshovr/bhvpain3.wav");
	sound_death = gi.soundindex ("bosshovr/bhvdeth1.wav");
	sound_search1 = gi.soundindex ("bosshovr/bhvunqv1.wav");

	gi.soundindex ("tank/rocket.wav");

	if (self->spawnflags & SPAWNFLAG_BOSS2_N64)
		gi.soundindex ("flyer/flyatck3.wav");
	else
		gi.soundindex ("infantry/infatck1.wav");

	self->s.sound = gi.soundindex ("bosshovr/bhvengn1.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/boss2/tris.md2");

	gi.modelindex ("models/monsters/boss2/gibs/chaingun.md2");
	gi.modelindex ("models/monsters/boss2/gibs/chest.md2");
	gi.modelindex ("models/monsters/boss2/gibs/cpu.md2");
	gi.modelindex ("models/monsters/boss2/gibs/engine.md2");
	gi.modelindex ("models/monsters/boss2/gibs/head.md2");
	gi.modelindex ("models/monsters/boss2/gibs/larm.md2");
	gi.modelindex ("models/monsters/boss2/gibs/rarm.md2");
	gi.modelindex ("models/monsters/boss2/gibs/rocket.md2");
	gi.modelindex ("models/monsters/boss2/gibs/spine.md2");
	gi.modelindex ("models/monsters/boss2/gibs/wing.md2");

	VectorSet (self->mins, -56, -56, 0);
	VectorSet (self->maxs, 56, 56, 80);

	self->health = 2000;
	self->gib_health = -200;
	self->mass = 1000;

	self->yaw_speed = 50;

	self->flags |= FL_IMMUNE_LASER;

	self->pain = boss2_pain;
	self->die = boss2_die;

	self->monsterinfo.stand = boss2_stand;
	self->monsterinfo.walk = boss2_walk;
	self->monsterinfo.run = boss2_run;
	self->monsterinfo.attack = boss2_attack;
	self->monsterinfo.search = boss2_search;
	self->monsterinfo.checkattack = Boss2_CheckAttack;
	self->monsterinfo.setskin = boss2_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &boss2_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	// [Paril-KEX]
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	flymonster_start (self);
}
