// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

BERSERK, the 2023 re-release's

Its m_berserk.cpp, in C, for its own maps (RR_Map): a berserker who
leaps at you from afar and slams down with a shockwave, swings as he
runs in, spikes and clubs up close, sidesteps and, rarely, dives
forward out of the way, jumps up and down ledges after you, and gibs
into pieces of his own. m_berserk.c is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10; its combat style is not
taken.
==============================================================================
*/

#include "g_local.h"
#include "m_berserk_rr.h"

void M_MonsterDodge (edict_t *self, edict_t *attacker, float eta, trace_t *tr);
qboolean blocked_checkplat (edict_t *self, float dist);

#define	SPAWNFLAG_BERSERK_NOJUMPING	8

static int	sound_pain;
static int	sound_die;
static int	sound_idle;
static int	sound_idle2;
static int	sound_punch;
static int	sound_sight;
static int	sound_search;
static int	sound_thud;
static int	sound_explod;
static int	sound_jump;

static void berserk_stand (edict_t *self);
static void berserk_run (edict_t *self);
static void berserk_fidget (edict_t *self);
static void berserk_dead (edict_t *self);
static mmove_t berserk_move_run_attack1;
static mmove_t berserk_move_jump;
static mmove_t berserk_move_jump2;

static void berserk_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void berserk_search (edict_t *self)
{
	if (rand() & 1)
		gi.sound (self, CHAN_VOICE, sound_idle2, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

static mframe_t berserk_frames_stand[] = {
	{ ai_stand, 0, berserk_fidget },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
static mmove_t berserk_move_stand = { FRAME_stand1, FRAME_stand5, berserk_frames_stand, NULL };

static void berserk_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &berserk_move_stand;
}

static mframe_t berserk_frames_stand_fidget[] = {
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
static mmove_t berserk_move_stand_fidget = { FRAME_standb1, FRAME_standb20, berserk_frames_stand_fidget, berserk_stand };

static void berserk_fidget (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		return;
	else if (self->enemy)
		return;
	if (random() > 0.15)
		return;

	self->monsterinfo.currentmove = &berserk_move_stand_fidget;
	gi.sound (self, CHAN_WEAPON, sound_idle, 1, ATTN_IDLE, 0);
}

static mframe_t berserk_frames_walk[] = {
	{ ai_walk, 9.1f },
	{ ai_walk, 6.3f },
	{ ai_walk, 4.9f },
	{ ai_walk, 6.7f, monster_footstep },
	{ ai_walk, 6.0f },
	{ ai_walk, 8.2f },
	{ ai_walk, 7.2f },
	{ ai_walk, 6.1f },
	{ ai_walk, 4.9f },
	{ ai_walk, 4.7f, monster_footstep },
	{ ai_walk, 4.7f }
};
static mmove_t berserk_move_walk = { FRAME_walkc1, FRAME_walkc11, berserk_frames_walk, NULL };

static void berserk_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &berserk_move_walk;
}

static mframe_t berserk_frames_run1[] = {
	{ ai_run, 21 },
	{ ai_run, 11, monster_footstep },
	{ ai_run, 21 },
	{ ai_run, 25, monster_done_dodge },
	{ ai_run, 18, monster_footstep },
	{ ai_run, 19 }
};
static mmove_t berserk_move_run1 = { FRAME_run1, FRAME_run6, berserk_frames_run1, NULL };

static void berserk_run (edict_t *self)
{
	monster_done_dodge (self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &berserk_move_stand;
	else
		self->monsterinfo.currentmove = &berserk_move_run1;
}

static void berserk_attack_spike (edict_t *self)
{
	vec3_t	aim = {MELEE_DISTANCE_RR, 0, -24};

	// faster attack -- upwards and backwards
	if (!fire_hit_rr (self, aim, 5 + rand() % 6, 80))
		self->monsterinfo.melee_debounce_time = level.time + 1.2;
}

static void berserk_swing (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_punch, 1, ATTN_NORM, 0);
}

static mframe_t berserk_frames_attack_spike[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, berserk_swing },
	{ ai_charge, 0, berserk_attack_spike },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t berserk_move_attack_spike = { FRAME_att_c1, FRAME_att_c8, berserk_frames_attack_spike, berserk_run };

static void berserk_attack_club (edict_t *self)
{
	vec3_t	aim;

	// slower attack
	VectorSet (aim, MELEE_DISTANCE_RR, self->mins[0], -4);
	if (!fire_hit_rr (self, aim, 15 + rand() % 6, 400))
		self->monsterinfo.melee_debounce_time = level.time + 2.5;
}

static mframe_t berserk_frames_attack_club[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, monster_footstep },
	{ ai_charge },
	{ ai_charge, 0, berserk_swing },
	{ ai_charge },
	{ ai_charge, 0, berserk_attack_club },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t berserk_move_attack_club = { FRAME_att_c9, FRAME_att_c20, berserk_frames_attack_club, berserk_run };

// the slam, where he lands, knocking up what is on the ground about it
static void berserk_attack_slam (edict_t *self)
{
	vec3_t	f, r, start, up = {0, 0, 1};
	vec3_t	offset = {20.0, -14.3, -21.0};
	trace_t	tr;

	gi.sound (self, CHAN_WEAPON, sound_thud, 1, ATTN_NORM, 0);
	gi.sound (self, CHAN_AUTO, sound_explod, 0.75, ATTN_NORM, 0);
	AngleVectors (self->s.angles, f, r, NULL);
	G_ProjectSource (self->s.origin, offset, f, r, start);
	tr = gi.trace (self->s.origin, NULL, NULL, start, self, MASK_SOLID);
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BERSERK_SLAM);
	gi.WritePosition (tr.endpos);
	gi.WriteDir (up);
	gi.multicast (tr.endpos, MULTICAST_PHS);
	self->gravity = 1.0;
	VectorClear (self->velocity);

	T_SlamRadiusDamage (tr.endpos, self, self, 8, 300, self, 165, MOD_UNKNOWN);
}

static void berserk_jump_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (self->health <= 0)
	{
		self->touch = NULL;
		return;
	}

	if (self->groundentity)
	{
		self->s.frame = FRAME_slam18;
		if (self->touch)
			berserk_attack_slam (self);
		self->touch = NULL;
	}
}

static void berserk_high_gravity (edict_t *self)
{
	if (self->velocity[2] < 0)
		self->gravity = 2.25 * (800 / sv_gravity->value);
	else
		self->gravity = 5.25 * (800 / sv_gravity->value);
}

static void berserk_jump_takeoff (edict_t *self)
{
	vec3_t	forward, dir, diff;
	float	fwd_speed;

	if (!self->enemy)
		return;

	// immediately turn to where we need to go
	VectorSubtract (self->s.origin, self->enemy->s.origin, diff);
	fwd_speed = VectorLength (diff) * 1.95;
	PredictAim (self, self->enemy, self->s.origin, fwd_speed, false, 0, dir, NULL);
	self->s.angles[1] = vectoyaw (dir);
	AngleVectors (self->s.angles, forward, NULL, NULL);
	self->s.origin[2] += 1;
	VectorScale (forward, fwd_speed, self->velocity);
	self->velocity[2] = 450;
	self->groundentity = NULL;
	self->monsterinfo.aiflags |= AI_DUCKED;
	self->monsterinfo.attack_finished = level.time + 3;
	self->touch = berserk_jump_touch;
	berserk_high_gravity (self);
}

static void berserk_check_landing (edict_t *self)
{
	berserk_high_gravity (self);

	if (self->groundentity)
	{
		self->monsterinfo.attack_finished = 0;
		self->monsterinfo.unduck (self);
		self->s.frame = FRAME_slam18;
		if (self->touch)
		{
			berserk_attack_slam (self);
			self->touch = NULL;
		}
		return;
	}

	if (level.time > self->monsterinfo.attack_finished)
		self->monsterinfo.nextframe = FRAME_slam3;
	else
		self->monsterinfo.nextframe = FRAME_slam5;
}

static mframe_t berserk_frames_attack_strike[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_move, 0, berserk_jump_takeoff },
	{ ai_move, 0, berserk_high_gravity },
	{ ai_move, 0, berserk_check_landing },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
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
	{ ai_move, 0, monster_footstep }
};
static mmove_t berserk_move_attack_strike = { FRAME_slam1, FRAME_slam23, berserk_frames_attack_strike, berserk_run };

static void berserk_melee (edict_t *self)
{
	if (self->monsterinfo.melee_debounce_time > level.time)
		return;
	// *almost* ready to land down the hammer from run-attack: don't switch
	else if (self->monsterinfo.currentmove == &berserk_move_run_attack1 && self->s.frame >= FRAME_r_att13)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		self->monsterinfo.attack_finished = 0;
		return;
	}

	monster_done_dodge (self);

	if (rand() & 1)
		self->monsterinfo.currentmove = &berserk_move_attack_spike;
	else
		self->monsterinfo.currentmove = &berserk_move_attack_club;
}

static void berserk_run_attack_speed (edict_t *self)
{
	if (self->enemy && range_to (self, self->enemy) < MELEE_DISTANCE_RR)
	{
		self->monsterinfo.nextframe = self->s.frame + 6;
		monster_done_dodge (self);
	}
}

static void berserk_run_attack_step (edict_t *self)
{
	berserk_run_attack_speed (self);
	monster_footstep (self);
}

static void berserk_run_attack_dodge (edict_t *self)
{
	berserk_run_attack_speed (self);
	monster_done_dodge (self);
}

static void berserk_run_swing (edict_t *self)
{
	berserk_swing (self);
	self->monsterinfo.melee_debounce_time = level.time + 0.6;

	if (self->monsterinfo.attack_state == AS_SLIDING)
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
		monster_done_dodge (self);
	}
}

static mframe_t berserk_frames_run_attack1[] = {
	{ ai_run, 21, berserk_run_attack_speed },
	{ ai_run, 11, berserk_run_attack_step },
	{ ai_run, 21, berserk_run_attack_speed },
	{ ai_run, 25, berserk_run_attack_dodge },
	{ ai_run, 18, berserk_run_attack_step },
	{ ai_run, 19, berserk_run_attack_speed },
	{ ai_run, 21 },
	{ ai_run, 11, monster_footstep },
	{ ai_run, 21 },
	{ ai_run, 25 },
	{ ai_run, 18, monster_footstep },
	{ ai_run, 19 },
	{ ai_run, 21, berserk_run_swing },
	{ ai_run, 11, monster_footstep },
	{ ai_run, 21 },
	{ ai_run, 25 },
	{ ai_run, 18, monster_footstep },
	{ ai_run, 19, berserk_attack_club }
};
static mmove_t berserk_move_run_attack1 = { FRAME_r_att1, FRAME_r_att18, berserk_frames_run_attack1, berserk_run };

static void berserk_attack (edict_t *self)
{
	if (self->monsterinfo.melee_debounce_time <= level.time && range_to (self, self->enemy) < MELEE_DISTANCE_RR)
		berserk_melee (self);
	// only jump if far enough away for it to make sense, not hopping
	// over and over again
	else if (!(self->spawnflags & SPAWNFLAG_BERSERK_NOJUMPING) && self->timestamp < level.time
		&& (rand() & 1) && range_to (self, self->enemy) > 150)
	{
		self->monsterinfo.currentmove = &berserk_move_attack_strike;
		// not again for a while
		gi.sound (self, CHAN_WEAPON, sound_jump, 1, ATTN_NORM, 0);
		self->timestamp = level.time + 5;
	}
	else if (self->monsterinfo.currentmove == &berserk_move_run1 && range_to (self, self->enemy) <= RANGE_NEAR_RR)
	{
		self->monsterinfo.currentmove = &berserk_move_run_attack1;
		self->monsterinfo.nextframe = FRAME_r_att1 + (self->s.frame - FRAME_run1) + 1;
	}
}

static mframe_t berserk_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t berserk_move_pain1 = { FRAME_painc1, FRAME_painc4, berserk_frames_pain1, berserk_run };

static mframe_t berserk_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep }
};
static mmove_t berserk_move_pain2 = { FRAME_painb1, FRAME_painb20, berserk_frames_pain2, berserk_run };

static void berserk_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	// if we're jumping, don't pain
	if (self->monsterinfo.currentmove == &berserk_move_jump
		|| self->monsterinfo.currentmove == &berserk_move_jump2
		|| self->monsterinfo.currentmove == &berserk_move_attack_strike)
		return;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;
	gi.sound (self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return;		// no pain anims in nightmare

	monster_done_dodge (self);

	if (damage <= 50 || random() < 0.5)
		self->monsterinfo.currentmove = &berserk_move_pain1;
	else
		self->monsterinfo.currentmove = &berserk_move_pain2;
}

static void berserk_setskin (edict_t *self)
{
	if (self->health < self->max_health / 2)
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static void berserk_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

static void berserk_shrink (edict_t *self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static mframe_t berserk_frames_death1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move, 0, berserk_shrink },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t berserk_move_death1 = { FRAME_death1, FRAME_death13, berserk_frames_death1, berserk_dead };

static mframe_t berserk_frames_death2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, berserk_shrink },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t berserk_move_death2 = { FRAME_deathc1, FRAME_deathc8, berserk_frames_death2, berserk_dead };

static gib_def_t berserk_gibs[] = {
	{ 2, "models/objects/gibs/bone/tris.md2" },
	{ 3, "models/objects/gibs/sm_meat/tris.md2" },
	{ 1, "models/objects/gibs/gear/tris.md2" },
	{ 1, "models/monsters/berserk/gibs/chest.md2", GIB_SKINNED },
	{ 1, "models/monsters/berserk/gibs/hammer.md2", GIB_SKINNED },
	{ 1, "models/monsters/berserk/gibs/thigh.md2", GIB_SKINNED },
	{ 1, "models/monsters/berserk/gibs/head.md2", GIB_HEAD | GIB_SKINNED },
	{ 0, NULL }
};

static void berserk_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		self->s.skinnum = 0;
		ThrowGibs (self, damage, berserk_gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	gi.sound (self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	if (damage >= 50)
		self->monsterinfo.currentmove = &berserk_move_death1;
	else
		self->monsterinfo.currentmove = &berserk_move_death2;
}

static void berserk_jump_now (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 100, forward, self->velocity);
	VectorMA (self->velocity, 300, up, self->velocity);
}

static void berserk_jump2_now (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 150, forward, self->velocity);
	VectorMA (self->velocity, 400, up, self->velocity);
}

static void berserk_jump_wait_land (edict_t *self)
{
	if (!self->groundentity)
	{
		self->monsterinfo.nextframe = self->s.frame;
		if (monster_jump_finished_rr (self))
			self->monsterinfo.nextframe = self->s.frame + 1;
	}
	else
		self->monsterinfo.nextframe = self->s.frame + 1;
}

static mframe_t berserk_frames_jump[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, berserk_jump_now },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, berserk_jump_wait_land },
	{ ai_move },
	{ ai_move }
};
static mmove_t berserk_move_jump = { FRAME_jump1, FRAME_jump9, berserk_frames_jump, berserk_run };

static mframe_t berserk_frames_jump2[] = {
	{ ai_move, -8 },
	{ ai_move, -4 },
	{ ai_move, -4 },
	{ ai_move, 0, berserk_jump2_now },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, berserk_jump_wait_land },
	{ ai_move },
	{ ai_move }
};
static mmove_t berserk_move_jump2 = { FRAME_jump1, FRAME_jump9, berserk_frames_jump2, berserk_run };

static void berserk_jump (edict_t *self, int result)
{
	if (!self->enemy)
		return;

	if (result == JUMP_UP)
		self->monsterinfo.currentmove = &berserk_move_jump2;
	else
		self->monsterinfo.currentmove = &berserk_move_jump;
}

static qboolean berserk_blocked (edict_t *self, float dist)
{
	int		result;

	if ((result = blocked_checkjump_rr (self, dist)) != JUMP_NO)
	{
		if (result != JUMP_TURN)
			berserk_jump (self, result);
		return true;
	}

	return blocked_checkplat (self, dist);
}

static qboolean berserk_sidestep (edict_t *self)
{
	// if we're jumping or in long pain, don't dodge
	if (self->monsterinfo.currentmove == &berserk_move_jump
		|| self->monsterinfo.currentmove == &berserk_move_jump2
		|| self->monsterinfo.currentmove == &berserk_move_attack_strike
		|| self->monsterinfo.currentmove == &berserk_move_pain2)
		return false;

	if (self->monsterinfo.currentmove != &berserk_move_run1)
		self->monsterinfo.currentmove = &berserk_move_run1;

	return true;
}

static mframe_t berserk_frames_duck[] = {
	{ ai_move, 0, monster_duck_down },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_duck_hold },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_duck_up },
	{ ai_move },
	{ ai_move }
};
static mmove_t berserk_move_duck = { FRAME_duck1, FRAME_duck10, berserk_frames_duck, berserk_run };

static mframe_t berserk_frames_duck2[] = {
	{ ai_move, 21, monster_duck_down },
	{ ai_move, 28 },
	{ ai_move, 20 },
	{ ai_move, 12, monster_footstep },
	{ ai_move, 7 },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move, 0, monster_duck_hold },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0 },
	{ ai_move, 0, monster_footstep },
	{ ai_move, 0, monster_duck_up },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
};
static mmove_t berserk_move_duck2 = { FRAME_fall2, FRAME_fall18, berserk_frames_duck2, berserk_run };

static qboolean berserk_duck (edict_t *self, float eta)
{
	// berserk only dives forward, and very rarely
	if (random() >= 0.05)
		return false;

	// if we're jumping, don't dodge
	if (self->monsterinfo.currentmove == &berserk_move_jump
		|| self->monsterinfo.currentmove == &berserk_move_jump2)
		return false;

	self->monsterinfo.currentmove = &berserk_move_duck2;
	return true;
}

void SP_monster_berserk_rr (edict_t *self)
{
	sound_pain = gi.soundindex ("berserk/berpain2.wav");
	sound_die = gi.soundindex ("berserk/berdeth2.wav");
	sound_idle = gi.soundindex ("berserk/beridle1.wav");
	sound_idle2 = gi.soundindex ("berserk/idle.wav");
	sound_punch = gi.soundindex ("berserk/attack.wav");
	sound_search = gi.soundindex ("berserk/bersrch1.wav");
	sound_sight = gi.soundindex ("berserk/sight.wav");
	sound_thud = gi.soundindex ("mutant/thud1.wav");
	sound_explod = gi.soundindex ("world/explod2.wav");
	sound_jump = gi.soundindex ("berserk/jump.wav");

	self->s.modelindex = gi.modelindex ("models/monsters/berserk/tris.md2");

	gi.modelindex ("models/monsters/berserk/gibs/head.md2");
	gi.modelindex ("models/monsters/berserk/gibs/chest.md2");
	gi.modelindex ("models/monsters/berserk/gibs/hammer.md2");
	gi.modelindex ("models/monsters/berserk/gibs/thigh.md2");

	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, 32);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	self->health = 240;
	self->gib_health = -60;
	self->mass = 250;

	self->pain = berserk_pain;
	self->die = berserk_die;

	self->monsterinfo.stand = berserk_stand;
	self->monsterinfo.walk = berserk_walk;
	self->monsterinfo.run = berserk_run;
	self->monsterinfo.dodge = (void (*)(edict_t *, edict_t *, float))M_MonsterDodge;
	self->monsterinfo.duck = berserk_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = berserk_sidestep;
	self->monsterinfo.blocked = berserk_blocked;
	self->monsterinfo.attack = berserk_attack;
	self->monsterinfo.melee = berserk_melee;
	self->monsterinfo.sight = berserk_sight;
	self->monsterinfo.search = berserk_search;
	self->monsterinfo.setskin = berserk_setskin;

	self->monsterinfo.currentmove = &berserk_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	self->monsterinfo.can_jump = !(self->spawnflags & SPAWNFLAG_BERSERK_NOJUMPING);
	self->monsterinfo.drop_height = 256;
	self->monsterinfo.jump_height = 40;

	gi.linkentity (self);

	walkmonster_start (self);
}
