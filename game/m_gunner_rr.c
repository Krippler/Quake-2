// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

GUNNER, the 2023 re-release's

Its m_gunner.cpp, in C, for its own maps (RR_Map): a gunner who
chainguns you up close and, from further off, lobs grenades judged to
reach you, from either of two throws; who blind-fires grenades where you
were last seen, uses his chaingun on a tesla, ducks (now and then
throwing a grenade as he goes down) and sidesteps, jumps up and down
ledges after you, and gibs into pieces of his own. m_gunner.c is 1997's,
for every other map. Its frames are its model's, as its gunner
commander's are (m_guncmdr.h).

What it does at 40 Hz is done at this game's 10.
==============================================================================
*/

#include "g_local.h"
#include "m_guncmdr.h"

void M_MonsterDodge (edict_t *self, edict_t *attacker, float eta, trace_t *tr);
qboolean blocked_checkplat (edict_t *self, float dist);

static int	sound_pain;
static int	sound_pain2;
static int	sound_death;
static int	sound_idle;
static int	sound_open;
static int	sound_search;
static int	sound_sight;

static void gunner_stand (edict_t *self);
static void gunner_run (edict_t *self);
static void gunner_dead (edict_t *self);
static void GunnerGrenade (edict_t *self);
static void GunnerFire (edict_t *self);
static void gunner_fire_chain (edict_t *self);
static void gunner_refire_chain (edict_t *self);
static mmove_t gunner_move_jump;
static mmove_t gunner_move_jump2;

static void gunner_idlesound (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static void gunner_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void gunner_search (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

static mframe_t gunner_frames_fidget[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, gunner_idlesound },
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
static mmove_t gunner_move_fidget = { FRAME_stand31, FRAME_stand70, gunner_frames_fidget, gunner_stand };

static void gunner_fidget (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		return;
	else if (self->enemy)
		return;
	if (random() <= 0.05)
		self->monsterinfo.currentmove = &gunner_move_fidget;
}

static mframe_t gunner_frames_stand[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, gunner_fidget },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, gunner_fidget },

	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, gunner_fidget }
};
static mmove_t gunner_move_stand = { FRAME_stand01, FRAME_stand30, gunner_frames_stand, NULL };

static void gunner_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &gunner_move_stand;
}

static mframe_t gunner_frames_walk[] = {
	{ ai_walk },
	{ ai_walk, 3 },
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 7 },
	{ ai_walk, 2, monster_footstep },
	{ ai_walk, 6 },
	{ ai_walk, 4 },
	{ ai_walk, 2 },
	{ ai_walk, 7 },
	{ ai_walk, 5 },
	{ ai_walk, 7 },
	{ ai_walk, 4, monster_footstep }
};
static mmove_t gunner_move_walk = { FRAME_walk07, FRAME_walk19, gunner_frames_walk, NULL };

static void gunner_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &gunner_move_walk;
}

static mframe_t gunner_frames_run[] = {
	{ ai_run, 26 },
	{ ai_run, 9, monster_footstep },
	{ ai_run, 9 },
	{ ai_run, 9, monster_done_dodge },
	{ ai_run, 15 },
	{ ai_run, 10, monster_footstep },
	{ ai_run, 13 },
	{ ai_run, 6 }
};
static mmove_t gunner_move_run = { FRAME_run01, FRAME_run08, gunner_frames_run, NULL };

static void gunner_run (edict_t *self)
{
	monster_done_dodge (self);
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &gunner_move_stand;
	else
		self->monsterinfo.currentmove = &gunner_move_run;
}

static mframe_t gunner_frames_runandshoot[] = {
	{ ai_run, 32 },
	{ ai_run, 15 },
	{ ai_run, 10 },
	{ ai_run, 18 },
	{ ai_run, 8 },
	{ ai_run, 20 }
};
static mmove_t gunner_move_runandshoot = { FRAME_runs01, FRAME_runs06, gunner_frames_runandshoot, NULL };

static mframe_t gunner_frames_pain3[] = {
	{ ai_move, -3 },
	{ ai_move, 1 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move, 1 }
};
static mmove_t gunner_move_pain3 = { FRAME_pain301, FRAME_pain305, gunner_frames_pain3, gunner_run };

static mframe_t gunner_frames_pain2[] = {
	{ ai_move, -2 },
	{ ai_move, 11 },
	{ ai_move, 6, monster_footstep },
	{ ai_move, 2 },
	{ ai_move, -1 },
	{ ai_move, -7 },
	{ ai_move, -2 },
	{ ai_move, -7, monster_footstep }
};
static mmove_t gunner_move_pain2 = { FRAME_pain201, FRAME_pain208, gunner_frames_pain2, gunner_run };

static mframe_t gunner_frames_pain1[] = {
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move, -5 },
	{ ai_move, 3 },
	{ ai_move, -1, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 1 },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move, 1, monster_footstep },
	{ ai_move },
	{ ai_move, -2 },
	{ ai_move, -2 },
	{ ai_move },
	{ ai_move, 0, monster_footstep }
};
static mmove_t gunner_move_pain1 = { FRAME_pain101, FRAME_pain118, gunner_frames_pain1, gunner_run };

static void gunner_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	monster_done_dodge (self);

	if (self->monsterinfo.currentmove == &gunner_move_jump
		|| self->monsterinfo.currentmove == &gunner_move_jump2)
		return;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	if (rand() & 1)
		gi.sound (self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return;		// no pain anims in nightmare

	if (damage <= 10)
		self->monsterinfo.currentmove = &gunner_move_pain3;
	else if (damage <= 25)
		self->monsterinfo.currentmove = &gunner_move_pain2;
	else
		self->monsterinfo.currentmove = &gunner_move_pain1;

	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up (self);
}

static void gunner_setskin (edict_t *self)
{
	if (self->health < self->max_health / 2)
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static void gunner_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

static void gunner_shrink (edict_t *self)
{
	self->maxs[2] = -4;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static mframe_t gunner_frames_death[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move, -7, gunner_shrink },
	{ ai_move, -3 },
	{ ai_move, -5 },
	{ ai_move, 8 },
	{ ai_move, 6 },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move }
};
static mmove_t gunner_move_death = { FRAME_death01, FRAME_death11, gunner_frames_death, gunner_dead };

static gib_def_t gunner_gibs[] = {
	{ 2, "models/objects/gibs/bone/tris.md2" },
	{ 2, "models/objects/gibs/sm_meat/tris.md2" },
	{ 1, "models/monsters/gunner/gibs/chest.md2", GIB_SKINNED },
	{ 1, "models/monsters/gunner/gibs/garm.md2", GIB_SKINNED },
	{ 1, "models/monsters/gunner/gibs/gun.md2", GIB_SKINNED },
	{ 1, "models/monsters/gunner/gibs/foot.md2", GIB_SKINNED },
	{ 1, "models/monsters/gunner/gibs/head.md2", GIB_SKINNED | GIB_HEAD },
	{ 0, NULL }
};

static void gunner_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		self->s.skinnum /= 2;
		ThrowGibs (self, damage, gunner_gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
	self->monsterinfo.currentmove = &gunner_move_death;
}

static mframe_t gunner_frames_duck[] = {
	{ ai_move, 1, monster_duck_down },
	{ ai_move, 1 },
	{ ai_move, 1, monster_duck_hold },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, 0, monster_duck_up },
	{ ai_move, -1 }
};
static mmove_t gunner_move_duck = { FRAME_duck01, FRAME_duck08, gunner_frames_duck, gunner_run };

static void gunner_opengun (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_open, 1, ATTN_IDLE, 0);
}

static void GunnerFire (edict_t *self)
{
	vec3_t	start, forward, right, aim;
	int		flash_number;

	if (!self->enemy || !self->enemy->inuse)
		return;

	flash_number = MZ2_GUNNER_MACHINEGUN_1 + (self->s.frame - FRAME_attak216);

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);
	PredictAim (self, self->enemy, start, 0, true, -0.2, aim, NULL);
	monster_fire_bullet (self, start, aim, 3, 4, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, flash_number);
}

// a grenade from where it would leave, reaching the enemy, and not too
// close
static qboolean gunner_grenade_check (edict_t *self)
{
	vec3_t	start, target, dir, aim;

	if (!self->enemy)
		return false;

	if (!M_CheckClearShot (self, monster_flash_offset[MZ2_GUNNER_GRENADE_1], start))
		return false;

	// blindfiring, at where the enemy was last seen
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
		VectorCopy (self->monsterinfo.blind_fire_target, target);
	else
		VectorCopy (self->enemy->s.origin, target);

	// too close?
	VectorSubtract (target, start, dir);
	if (VectorLength (dir) < 100)
		return false;

	// can it be thrown to the enemy?
	VectorCopy (dir, aim);
	VectorNormalize (aim);
	return M_CalculatePitchToFire (self, target, start, aim, 600, 2.5, false, false);
}

static void GunnerGrenade (edict_t *self)
{
	vec3_t		start, forward, right, up, aim, target;
	int			flash_number;
	float		spread, pitch = 0, dist;
	qboolean	blindfire = false;

	if (!self->enemy || !self->enemy->inuse)
		return;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
		blindfire = true;

	if (self->s.frame == FRAME_attak105 || self->s.frame == FRAME_attak309)
	{
		spread = -0.10;
		flash_number = MZ2_GUNNER_GRENADE_1;
	}
	else if (self->s.frame == FRAME_attak108 || self->s.frame == FRAME_attak312)
	{
		spread = -0.05;
		flash_number = MZ2_GUNNER_GRENADE_2;
	}
	else if (self->s.frame == FRAME_attak111 || self->s.frame == FRAME_attak315)
	{
		spread = 0.05;
		flash_number = MZ2_GUNNER_GRENADE_3;
	}
	else	// (self->s.frame == FRAME_attak114)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		spread = 0.10;
		flash_number = MZ2_GUNNER_GRENADE_4;
	}

	// the other throw, from the front
	if (self->s.frame >= FRAME_attak301 && self->s.frame <= FRAME_attak324)
		flash_number = MZ2_GUNNER_GRENADE2_1 + (MZ2_GUNNER_GRENADE_4 - flash_number);

	// shooting blind and still can't see the enemy: at where it was
	if (blindfire && !visible (self, self->enemy))
	{
		if (VectorCompare (self->monsterinfo.blind_fire_target, vec3_origin))
			return;
		VectorCopy (self->monsterinfo.blind_fire_target, target);
	}
	else
		VectorCopy (self->enemy->s.origin, target);

	AngleVectors (self->s.angles, forward, right, up);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);

	VectorSubtract (target, self->s.origin, aim);
	dist = VectorLength (aim);

	// aim up if they're on the same level as me and far away
	if (dist > 512 && aim[2] < 64 && aim[2] > -64)
		aim[2] += dist - 512;

	VectorNormalize (aim);
	pitch = aim[2];
	if (pitch > 0.4)
		pitch = 0.4;
	else if (pitch < -0.5)
		pitch = -0.5;

	VectorMA (forward, spread, right, aim);
	VectorMA (aim, pitch, up, aim);

	// the best pitch, if there is one; or a normal shot
	if (M_CalculatePitchToFire (self, target, start, aim, 600, 2.5, false, false))
		fire_grenade_adjust (self, start, aim, 50, 600, 2.5, 90, crandom() * 10, random() * 10);
	else
		fire_grenade_adjust (self, start, aim, 50, 600, 2.5, 90, crandom() * 10, 200 + crandom() * 10);
	monster_muzzleflash (self, start, flash_number);
}

static mframe_t gunner_frames_attack_chain[] = {
	{ ai_charge, 0, gunner_opengun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t gunner_move_attack_chain = { FRAME_attak209, FRAME_attak215, gunner_frames_attack_chain, gunner_fire_chain };

static mframe_t gunner_frames_fire_chain[] = {
	{ ai_charge, 0, GunnerFire },
	{ ai_charge, 0, GunnerFire },
	{ ai_charge, 0, GunnerFire },
	{ ai_charge, 0, GunnerFire },
	{ ai_charge, 0, GunnerFire },
	{ ai_charge, 0, GunnerFire },
	{ ai_charge, 0, GunnerFire },
	{ ai_charge, 0, GunnerFire }
};
static mmove_t gunner_move_fire_chain = { FRAME_attak216, FRAME_attak223, gunner_frames_fire_chain, gunner_refire_chain };

static mframe_t gunner_frames_endfire_chain[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, monster_footstep }
};
static mmove_t gunner_move_endfire_chain = { FRAME_attak224, FRAME_attak230, gunner_frames_endfire_chain, gunner_run };

static void gunner_blind_check (edict_t *self)
{
	vec3_t	aim;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		VectorSubtract (self->monsterinfo.blind_fire_target, self->s.origin, aim);
		self->ideal_yaw = vectoyaw (aim);
	}
}

static mframe_t gunner_frames_attack_grenade[] = {
	{ ai_charge, 0, gunner_blind_check },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GunnerGrenade },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GunnerGrenade },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GunnerGrenade },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GunnerGrenade },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t gunner_move_attack_grenade = { FRAME_attak101, FRAME_attak121, gunner_frames_attack_grenade, gunner_run };

static mframe_t gunner_frames_attack_grenade2[] = {
	//{ ai_charge },
	//{ ai_charge },
	//{ ai_charge },
	//{ ai_charge },

	{ ai_charge, 0, gunner_blind_check },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GunnerGrenade },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GunnerGrenade },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GunnerGrenade },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, GunnerGrenade },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t gunner_move_attack_grenade2 = { FRAME_attak305, FRAME_attak324, gunner_frames_attack_grenade2, gunner_run };

static void gunner_attack (edict_t *self)
{
	float	chance, r;
	vec3_t	clear;

	monster_done_dodge (self);

	if (self->monsterinfo.attack_state == AS_BLIND)
	{
		if (self->timestamp > level.time)
			return;

		// setup shot probabilities
		if (self->monsterinfo.blind_fire_delay < 1)
			chance = 1.0;
		else if (self->monsterinfo.blind_fire_delay < 7.5)
			chance = 0.4;
		else
			chance = 0.1;

		r = random();

		// minimum of 4.1 seconds, plus 0-3, after the shots are done
		self->monsterinfo.blind_fire_delay += 4.1 + random() * 3;

		// don't shoot at the origin
		if (VectorCompare (self->monsterinfo.blind_fire_target, vec3_origin))
			return;

		// don't shoot if the dice say not to
		if (r > chance)
			return;

		// turn on manual steering to signal both manual steering and blindfire
		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;

		if (gunner_grenade_check (self))
		{
			self->monsterinfo.currentmove = (rand() & 1) ? &gunner_move_attack_grenade2 : &gunner_move_attack_grenade;
			self->monsterinfo.attack_finished = level.time + random() * 2;
		}
		else
			self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

		self->timestamp = level.time + 2 + random();
		return;
	}

	// his chaingun on a tesla, or close, or between grenades
	if (self->bad_area || self->timestamp > level.time
		|| (range_to (self, self->enemy) <= RANGE_NEAR_RR * 0.35
			&& M_CheckClearShot (self, monster_flash_offset[MZ2_GUNNER_MACHINEGUN_1], clear)))
		self->monsterinfo.currentmove = &gunner_move_attack_chain;
	else if (self->timestamp <= level.time && random() <= 0.5 && gunner_grenade_check (self))
	{
		self->monsterinfo.currentmove = (rand() & 1) ? &gunner_move_attack_grenade2 : &gunner_move_attack_grenade;
		self->timestamp = level.time + 2 + random();
	}
	else if (M_CheckClearShot (self, monster_flash_offset[MZ2_GUNNER_MACHINEGUN_1], clear))
		self->monsterinfo.currentmove = &gunner_move_attack_chain;
}

static void gunner_fire_chain (edict_t *self)
{
	self->monsterinfo.currentmove = &gunner_move_fire_chain;
}

static void gunner_refire_chain (edict_t *self)
{
	if (self->enemy->health > 0 && visible (self, self->enemy) && random() <= 0.5)
	{
		self->monsterinfo.currentmove = &gunner_move_fire_chain;
		return;
	}
	self->monsterinfo.currentmove = &gunner_move_endfire_chain;
}

static void gunner_jump_now (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 100, forward, self->velocity);
	VectorMA (self->velocity, 300, up, self->velocity);
}

static void gunner_jump2_now (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 150, forward, self->velocity);
	VectorMA (self->velocity, 400, up, self->velocity);
}

static void gunner_jump_wait_land (edict_t *self)
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

static mframe_t gunner_frames_jump[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, gunner_jump_now },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, gunner_jump_wait_land },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t gunner_move_jump = { FRAME_jump01, FRAME_jump10, gunner_frames_jump, gunner_run };

static mframe_t gunner_frames_jump2[] = {
	{ ai_move, -8 },
	{ ai_move, -4 },
	{ ai_move, -4 },
	{ ai_move, 0, gunner_jump2_now },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, gunner_jump_wait_land },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t gunner_move_jump2 = { FRAME_jump01, FRAME_jump10, gunner_frames_jump2, gunner_run };

static void gunner_jump (edict_t *self, int result)
{
	if (!self->enemy)
		return;

	monster_done_dodge (self);

	if (result == JUMP_UP)
		self->monsterinfo.currentmove = &gunner_move_jump2;
	else
		self->monsterinfo.currentmove = &gunner_move_jump;
}

static qboolean gunner_blocked (edict_t *self, float dist)
{
	int		result;

	if (blocked_checkplat (self, dist))
		return true;

	if ((result = blocked_checkjump_rr (self, dist)) != JUMP_NO)
	{
		if (result != JUMP_TURN)
			gunner_jump (self, result);
		return true;
	}

	return false;
}

static qboolean gunner_duck (edict_t *self, float eta)
{
	if (self->monsterinfo.currentmove == &gunner_move_jump2
		|| self->monsterinfo.currentmove == &gunner_move_jump)
		return false;

	if (self->monsterinfo.currentmove == &gunner_move_attack_chain
		|| self->monsterinfo.currentmove == &gunner_move_fire_chain
		|| self->monsterinfo.currentmove == &gunner_move_attack_grenade
		|| self->monsterinfo.currentmove == &gunner_move_attack_grenade2)
	{
		// if we're shooting don't dodge
		self->monsterinfo.unduck (self);
		return false;
	}

	if (random() > 0.5)
		GunnerGrenade (self);

	self->monsterinfo.currentmove = &gunner_move_duck;
	return true;
}

static qboolean gunner_sidestep (edict_t *self)
{
	if (self->monsterinfo.currentmove == &gunner_move_jump2
		|| self->monsterinfo.currentmove == &gunner_move_jump
		|| self->monsterinfo.currentmove == &gunner_move_pain1)
		return false;

	if (self->monsterinfo.currentmove == &gunner_move_attack_chain
		|| self->monsterinfo.currentmove == &gunner_move_fire_chain
		|| self->monsterinfo.currentmove == &gunner_move_attack_grenade
		|| self->monsterinfo.currentmove == &gunner_move_attack_grenade2)
		return false;	// if we're shooting, don't dodge

	if (self->monsterinfo.currentmove != &gunner_move_run)
		self->monsterinfo.currentmove = &gunner_move_run;

	return true;
}

#define	SPAWNFLAG_GUNNER_NOJUMPING	8

void SP_monster_gunner_rr (edict_t *self)
{
	sound_death = gi.soundindex ("gunner/death1.wav");
	sound_pain = gi.soundindex ("gunner/gunpain2.wav");
	sound_pain2 = gi.soundindex ("gunner/gunpain1.wav");
	sound_idle = gi.soundindex ("gunner/gunidle1.wav");
	sound_open = gi.soundindex ("gunner/gunatck1.wav");
	sound_search = gi.soundindex ("gunner/gunsrch1.wav");
	sound_sight = gi.soundindex ("gunner/sight1.wav");

	gi.soundindex ("gunner/gunatck2.wav");
	gi.soundindex ("gunner/gunatck3.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/gunner/tris.md2");

	gi.modelindex ("models/monsters/gunner/gibs/chest.md2");
	gi.modelindex ("models/monsters/gunner/gibs/foot.md2");
	gi.modelindex ("models/monsters/gunner/gibs/garm.md2");
	gi.modelindex ("models/monsters/gunner/gibs/gun.md2");
	gi.modelindex ("models/monsters/gunner/gibs/head.md2");

	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, 36);

	self->health = 175;
	self->gib_health = -70;
	self->mass = 200;

	self->pain = gunner_pain;
	self->die = gunner_die;

	self->monsterinfo.stand = gunner_stand;
	self->monsterinfo.walk = gunner_walk;
	self->monsterinfo.run = gunner_run;
	self->monsterinfo.dodge = (void (*)(edict_t *, edict_t *, float))M_MonsterDodge;
	self->monsterinfo.duck = gunner_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = gunner_sidestep;
	self->monsterinfo.blocked = gunner_blocked;
	self->monsterinfo.attack = gunner_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = gunner_sight;
	self->monsterinfo.search = gunner_search;
	self->monsterinfo.setskin = gunner_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &gunner_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	self->monsterinfo.blindfire = true;
	self->monsterinfo.can_jump = !(self->spawnflags & SPAWNFLAG_GUNNER_NOJUMPING);
	self->monsterinfo.drop_height = 192;
	self->monsterinfo.jump_height = 40;

	walkmonster_start (self);
}
