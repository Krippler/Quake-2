// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

INFANTRY, the 2023 re-release's

Its m_infantry.cpp, in C, for its own maps (RR_Map): an infantryman who
runs at you firing, cocks his gun before he fires unless it is cocked
already, punches up close, ducks and sidesteps, jumps up and down
ledges after you, sprays his gun as he dies, now and then loses his
head, and gibs into pieces of his own. m_infantry.c is 1997's, for
every other map.

What it does at 40 Hz is done at this game's 10; its combat style and
sidestep speed are not taken.
==============================================================================
*/

#include "g_local.h"
#include "m_infantry_rr.h"

void M_MonsterDodge (edict_t *self, edict_t *attacker, float eta, trace_t *tr);
qboolean blocked_checkplat (edict_t *self, float dist);

static int	sound_pain1;
static int	sound_pain2;
static int	sound_die1;
static int	sound_die2;

static int	sound_gunshot;
static int	sound_weapon_cock;
static int	sound_punch_swing;
static int	sound_punch_hit;
static int	sound_sight;
static int	sound_search;
static int	sound_idle;

// range at which he'll try to start a run-attack to close the distance
#define	RANGE_RUN_ATTACK	(RANGE_NEAR_RR * 0.75)

static void infantry_stand (edict_t *self);
static void infantry_run (edict_t *self);
static void infantry_dead (edict_t *self);
static void infantry_fire (edict_t *self);
static void InfantryMachineGun (edict_t *self);
static mmove_t infantry_move_jump;
static mmove_t infantry_move_jump2;
static mmove_t infantry_move_attack1;
static mmove_t infantry_move_attack4;

static mframe_t infantry_frames_stand[] = {
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
static mmove_t infantry_move_stand = { FRAME_stand50, FRAME_stand71, infantry_frames_stand, NULL };

static void infantry_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &infantry_move_stand;
}

static mframe_t infantry_frames_fidget[] = {
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand, 3 },
	{ ai_stand, 6 },
	{ ai_stand, 3, monster_footstep },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand, -2 },
	{ ai_stand, 1 },
	{ ai_stand, 1 },
	{ ai_stand, 1 },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 1 },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, -1 },
	{ ai_stand, -1 },
	{ ai_stand },
	{ ai_stand, -3 },
	{ ai_stand, -2 },
	{ ai_stand, -3 },
	{ ai_stand, -3, monster_footstep },
	{ ai_stand, -2 }
};
static mmove_t infantry_move_fidget = { FRAME_stand01, FRAME_stand49, infantry_frames_fidget, infantry_stand };

static void infantry_fidget (edict_t *self)
{
	if (self->enemy)
		return;

	self->monsterinfo.currentmove = &infantry_move_fidget;
	gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static mframe_t infantry_frames_walk[] = {
	{ ai_walk, 5, monster_footstep },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 6, monster_footstep },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 4 },
	{ ai_walk, 5 }
};
static mmove_t infantry_move_walk = { FRAME_walk03, FRAME_walk14, infantry_frames_walk, NULL };

static void infantry_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &infantry_move_walk;
}

static mframe_t infantry_frames_run[] = {
	{ ai_run, 10 },
	{ ai_run, 15, monster_footstep },
	{ ai_run, 5 },
	{ ai_run, 7, monster_done_dodge },
	{ ai_run, 18 },
	{ ai_run, 20, monster_footstep },
	{ ai_run, 2 },
	{ ai_run, 6 }
};
static mmove_t infantry_move_run = { FRAME_run01, FRAME_run08, infantry_frames_run, NULL };

static void infantry_run (edict_t *self)
{
	monster_done_dodge (self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &infantry_move_stand;
	else
		self->monsterinfo.currentmove = &infantry_move_run;
}

static mframe_t infantry_frames_pain1[] = {
	{ ai_move, -3 },
	{ ai_move, -2 },
	{ ai_move, -1 },
	{ ai_move, -2 },
	{ ai_move, -1, monster_footstep },
	{ ai_move, 1 },
	{ ai_move, -1 },
	{ ai_move, 1 },
	{ ai_move, 6 },
	{ ai_move, 2, monster_footstep }
};
static mmove_t infantry_move_pain1 = { FRAME_pain101, FRAME_pain110, infantry_frames_pain1, infantry_run };

static mframe_t infantry_frames_pain2[] = {
	{ ai_move, -3 },
	{ ai_move, -3 },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -2, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 5 },
	{ ai_move, 2, monster_footstep }
};
static mmove_t infantry_move_pain2 = { FRAME_pain201, FRAME_pain210, infantry_frames_pain2, infantry_run };

static void infantry_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	int		n;

	// allow turret to pain
	if ((self->monsterinfo.currentmove == &infantry_move_jump
		|| self->monsterinfo.currentmove == &infantry_move_jump2) && self->think == monster_think)
		return;

	monster_done_dodge (self);

	if (level.time < self->pain_debounce_time)
	{
		if (self->think == monster_think && random() < 0.33)
			M_MonsterDodge (self, other, FRAMETIME, NULL);
		return;
	}

	self->pain_debounce_time = level.time + 3;

	n = rand() & 1;
	if (n == 0)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	if (self->think != monster_think)
		return;

	if (!M_ShouldReactToPain (self))
	{
		if (self->think == monster_think && random() < 0.33)
			M_MonsterDodge (self, other, FRAMETIME, NULL);
		return;		// no pain anims in nightmare
	}

	if (n == 0)
		self->monsterinfo.currentmove = &infantry_move_pain1;
	else
		self->monsterinfo.currentmove = &infantry_move_pain2;

	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up (self);
}

static void infantry_setskin (edict_t *self)
{
	if (self->health < self->max_health / 2)
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static vec3_t aimangles[] = {
	{ 0.0, 5.0, 0.0 },
	{ 10.0, 15.0, 0.0 },
	{ 20.0, 25.0, 0.0 },
	{ 25.0, 35.0, 0.0 },
	{ 30.0, 40.0, 0.0 },
	{ 30.0, 45.0, 0.0 },
	{ 25.0, 50.0, 0.0 },
	{ 20.0, 40.0, 0.0 },
	{ 15.0, 35.0, 0.0 },
	{ 40.0, 35.0, 0.0 },
	{ 70.0, 35.0, 0.0 },
	{ 90.0, 35.0, 0.0 }
};

static void InfantryMachineGun (edict_t *self)
{
	vec3_t		start, forward, right, vec;
	int			flash_number;
	qboolean	is_run_attack;

	if (!self->enemy || !self->enemy->inuse)
		return;

	is_run_attack = self->s.frame >= FRAME_run201 && self->s.frame <= FRAME_run208;

	if (self->s.frame == FRAME_attak103 || self->s.frame == FRAME_attak311 || is_run_attack || self->s.frame == FRAME_attak416)
	{
		if (is_run_attack)
			flash_number = MZ2_INFANTRY_MACHINEGUN_14 + (self->s.frame - FRAME_run201);
		else if (self->s.frame == FRAME_attak416)
			flash_number = MZ2_INFANTRY_MACHINEGUN_22;
		else
			flash_number = MZ2_INFANTRY_MACHINEGUN_1;
		AngleVectors (self->s.angles, forward, right, NULL);
		G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);
		PredictAim (self, self->enemy, start, 0, true, -0.2, forward, NULL);
	}
	else
	{
		// dying, spraying round
		flash_number = MZ2_INFANTRY_MACHINEGUN_2 + (self->s.frame - FRAME_death211);

		AngleVectors (self->s.angles, forward, right, NULL);
		G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);

		VectorSubtract (self->s.angles, aimangles[flash_number - MZ2_INFANTRY_MACHINEGUN_2], vec);
		AngleVectors (vec, forward, NULL, NULL);
	}

	monster_fire_bullet (self, start, forward, 3, 4, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, flash_number);
}

static void infantry_sight (edict_t *self, edict_t *other)
{
	if (rand() & 1)
		gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

static void infantry_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

static void infantry_shrink (edict_t *self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static void infantry_shrink_step (edict_t *self)
{
	infantry_shrink (self);
	monster_footstep (self);
}

static void infantry_death3_skip (edict_t *self)
{
	self->monsterinfo.nextframe = FRAME_death307;
}

static mframe_t infantry_frames_death1[] = {
	{ ai_move, -4, NULL },
	{ ai_move },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -4, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -1, monster_footstep },
	{ ai_move, 3 },
	{ ai_move, 1 },
	{ ai_move, 1 },
	{ ai_move, -2 },
	{ ai_move, 2 },
	{ ai_move, 2 },
	{ ai_move, 9, infantry_shrink_step },
	{ ai_move, 9 },
	{ ai_move, 5, monster_footstep },
	{ ai_move, -3 },
	{ ai_move, -3 }
};
static mmove_t infantry_move_death1 = { FRAME_death101, FRAME_death120, infantry_frames_death1, infantry_dead };

// Off with his head
static mframe_t infantry_frames_death2[] = {
	{ ai_move, 0, NULL },
	{ ai_move, 1 },
	{ ai_move, 5 },
	{ ai_move, -1 },
	{ ai_move },
	{ ai_move, 1, monster_footstep },
	{ ai_move, 1, monster_footstep },
	{ ai_move, 4 },
	{ ai_move, 3 },
	{ ai_move },
	{ ai_move, -2, InfantryMachineGun },
	{ ai_move, -2, InfantryMachineGun },
	{ ai_move, -3, InfantryMachineGun },
	{ ai_move, -1, InfantryMachineGun },
	{ ai_move, -2, InfantryMachineGun },
	{ ai_move, 0, InfantryMachineGun },
	{ ai_move, 2, InfantryMachineGun },
	{ ai_move, 2, InfantryMachineGun },
	{ ai_move, 3, InfantryMachineGun },
	{ ai_move, -10, InfantryMachineGun },
	{ ai_move, -7, InfantryMachineGun },
	{ ai_move, -8, InfantryMachineGun },
	{ ai_move, -6, infantry_shrink_step },
	{ ai_move, 4 },
	{ ai_move }
};
static mmove_t infantry_move_death2 = { FRAME_death201, FRAME_death225, infantry_frames_death2, infantry_dead };

static mframe_t infantry_frames_death3[] = {
	{ ai_move, 0 },
	{ ai_move },
	{ ai_move, 0, infantry_shrink_step },
	{ ai_move, -6 },
	{ ai_move, -11, infantry_death3_skip },
	{ ai_move, -3 },
	{ ai_move, -11 },
	{ ai_move, 0, monster_footstep },
	{ ai_move }
};
static mmove_t infantry_move_death3 = { FRAME_death301, FRAME_death309, infantry_frames_death3, infantry_dead };

static void infantry_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	int			n;
	edict_t		*head;
	vec3_t		dir;
	gib_def_t	gibs[] = {
		{ 1, "models/objects/gibs/bone/tris.md2" },
		{ 3, "models/objects/gibs/sm_meat/tris.md2" },
		{ 1, "models/monsters/infantry/gibs/chest.md2", GIB_SKINNED },
		{ 1, "models/monsters/infantry/gibs/gun.md2", GIB_SKINNED },
		{ 2, "models/monsters/infantry/gibs/foot.md2", GIB_SKINNED },
		{ 2, "models/monsters/infantry/gibs/arm.md2", GIB_SKINNED },
		{ 1, NULL, GIB_HEAD | GIB_SKINNED },
		{ 0, NULL }
	};

	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);

		// his head's gone already in the third death
		gibs[6].gibname = self->monsterinfo.currentmove != &infantry_move_death3
			? "models/objects/gibs/sm_meat/tris.md2" : "models/monsters/infantry/gibs/head.md2";
		self->s.skinnum /= 2;
		ThrowGibs (self, damage, gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	n = rand() % 3;
	if (n == 0)
	{
		self->monsterinfo.currentmove = &infantry_move_death1;
		gi.sound (self, CHAN_VOICE, sound_die2, 1, ATTN_NORM, 0);
	}
	else if (n == 1)
	{
		self->monsterinfo.currentmove = &infantry_move_death2;
		gi.sound (self, CHAN_VOICE, sound_die1, 1, ATTN_NORM, 0);
	}
	else
	{
		self->monsterinfo.currentmove = &infantry_move_death3;
		gi.sound (self, CHAN_VOICE, sound_die2, 1, ATTN_NORM, 0);
	}

	// don't always pop a head gib, it gets old
	if (n != 2 && random() <= 0.25)
	{
		head = ThrowGib (self, "models/monsters/infantry/gibs/head.md2", damage, GIB_ORGANIC);
		if (head)
		{
			VectorCopy (self->s.angles, head->s.angles);
			VectorCopy (self->s.origin, head->s.origin);
			head->s.origin[2] += 32;
			VectorSubtract (self->s.origin, inflictor->s.origin, dir);
			VectorNormalize (dir);
			VectorScale (dir, 100, head->velocity);
			head->velocity[2] = 200;
			VectorScale (head->avelocity, 0.15, head->avelocity);
			head->s.skinnum = 0;
			gi.linkentity (head);
		}
	}
}

static mframe_t infantry_frames_duck[] = {
	{ ai_move, -2, monster_duck_down },
	{ ai_move, -5, monster_duck_hold },
	{ ai_move, 3 },
	{ ai_move, 4, monster_duck_up },
	{ ai_move }
};
static mmove_t infantry_move_duck = { FRAME_duck01, FRAME_duck05, infantry_frames_duck, infantry_run };

static void infantry_set_firetime (edict_t *self)
{
	self->monsterinfo.fire_wait = level.time + 0.7 + random() * 1.3;

	if (!(self->monsterinfo.aiflags & AI_STAND_GROUND) && self->enemy
		&& range_to (self, self->enemy) >= RANGE_RUN_ATTACK && ai_check_move (self, 8))
		self->monsterinfo.currentmove = &infantry_move_attack4;
}

static void infantry_set_firetime_step (edict_t *self)
{
	infantry_set_firetime (self);
	monster_footstep (self);
}

static void infantry_cock_gun (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_weapon_cock, 1, ATTN_NORM, 0);

	// gun cocked
	self->count = 1;
}

static void infantry_attack1_skip (edict_t *self)
{
	self->monsterinfo.nextframe = FRAME_attak114;
	monster_footstep (self);
}

static void infantry_next_frame (edict_t *self)
{
	self->monsterinfo.nextframe = self->s.frame + 1;
}

// cock-less attack, used if he has already cocked his gun
static mframe_t infantry_frames_attack1[] = {
	{ ai_charge },
	{ ai_charge, 6, infantry_set_firetime_step },
	{ ai_charge, 0, infantry_fire },
	{ ai_charge },
	{ ai_charge, 1 },
	{ ai_charge, -7 },
	{ ai_charge, -6, infantry_attack1_skip },
	// dead frames start
	{ ai_charge, -1 },
	{ ai_charge, 0, infantry_cock_gun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	// dead frames end
	{ ai_charge, -1 },
	{ ai_charge, -1 }
};
static mmove_t infantry_move_attack1 = { FRAME_attak101, FRAME_attak115, infantry_frames_attack1, infantry_run };

// old animation, full cock + shoot
static mframe_t infantry_frames_attack3[] = {
	{ ai_charge, 4,  NULL },
	{ ai_charge, -1, NULL },
	{ ai_charge, -1, NULL },
	{ ai_charge, 0,  infantry_cock_gun },
	{ ai_charge, -1, NULL },
	{ ai_charge, 1,  NULL },
	{ ai_charge, 1,  NULL },
	{ ai_charge, 2,  NULL },
	{ ai_charge, -2, NULL },
	{ ai_charge, -3, infantry_set_firetime_step  },
	{ ai_charge, 1,  infantry_fire },
	{ ai_charge, 5,  NULL },
	{ ai_charge, -1, NULL },
	{ ai_charge, -2, NULL },
	{ ai_charge, -3, NULL },
};
static mmove_t infantry_move_attack3 = { FRAME_attak301, FRAME_attak315, infantry_frames_attack3, infantry_run };

// even older animation, full cock + shoot
static mframe_t infantry_frames_attack5[] = {
	// skipped frames
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },

	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, monster_footstep },
	{ ai_charge, 0, infantry_cock_gun },
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, infantry_next_frame },
	{ ai_charge, 0, NULL }, // skipped frame
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, infantry_set_firetime },
	{ ai_charge, 0, infantry_fire },

	// skipped frames
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },
	
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, NULL },
	{ ai_charge, 0, monster_footstep }
};
static mmove_t infantry_move_attack5 = { FRAME_attak401, FRAME_attak423, infantry_frames_attack5, infantry_run };

static void infantry_fire (edict_t *self)
{
	InfantryMachineGun (self);

	// we fired, so we must cock again before firing
	self->count = 0;

	// check if we ran out of firing time
	if (self->monsterinfo.currentmove == &infantry_move_attack4)
	{
		if (level.time >= self->monsterinfo.fire_wait)
		{
			monster_done_dodge (self);
			self->monsterinfo.currentmove = &infantry_move_attack1;
			self->monsterinfo.nextframe = FRAME_attak114;
		}
		// got close to an edge
		else if (!ai_check_move (self, 8))
		{
			self->monsterinfo.currentmove = &infantry_move_attack1;
			self->monsterinfo.nextframe = FRAME_attak103;
			monster_done_dodge (self);
			self->monsterinfo.attack_state = AS_STRAIGHT;
		}
	}
	else if ((self->s.frame >= FRAME_attak101 && self->s.frame <= FRAME_attak115)
		|| (self->s.frame >= FRAME_attak301 && self->s.frame <= FRAME_attak315)
		|| (self->s.frame >= FRAME_attak401 && self->s.frame <= FRAME_attak423))
	{
		if (level.time >= self->monsterinfo.fire_wait)
		{
			self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

			if (self->s.frame == FRAME_attak416)
				self->monsterinfo.nextframe = FRAME_attak420;
		}
		else
			self->monsterinfo.aiflags |= AI_HOLD_FRAME;
	}
}

static void infantry_swing (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_punch_swing, 1, ATTN_NORM, 0);
}

static void infantry_smack (edict_t *self)
{
	vec3_t	aim = {MELEE_DISTANCE_RR, 0, 0};

	if (fire_hit_rr (self, aim, 5 + rand() % 5, 50))
		gi.sound (self, CHAN_WEAPON, sound_punch_hit, 1, ATTN_NORM, 0);
	else
		self->monsterinfo.melee_debounce_time = level.time + 1.5;
}

static mframe_t infantry_frames_attack2[] = {
	{ ai_charge, 3 },
	{ ai_charge, 6 },
	{ ai_charge, 0, infantry_swing },
	{ ai_charge, 8, monster_footstep },
	{ ai_charge, 5 },
	{ ai_charge, 8, infantry_smack },
	{ ai_charge, 6 },
	{ ai_charge, 3 }
};
static mmove_t infantry_move_attack2 = { FRAME_attak201, FRAME_attak208, infantry_frames_attack2, infantry_run };

// run-attack, inspired by q2test
static void infantry_attack4_refire (edict_t *self)
{
	// ran out of firing time
	if (level.time >= self->monsterinfo.fire_wait)
	{
		monster_done_dodge (self);
		self->monsterinfo.currentmove = &infantry_move_attack1;
		self->monsterinfo.nextframe = FRAME_attak114;
	}
	// too close, or can't go on, back to the regular attack
	else if ((self->monsterinfo.aiflags & AI_STAND_GROUND)
		|| (self->enemy && (range_to (self, self->enemy) < RANGE_RUN_ATTACK || !ai_check_move (self, 8))))
	{
		self->monsterinfo.currentmove = &infantry_move_attack1;
		self->monsterinfo.nextframe = FRAME_attak103;
		monster_done_dodge (self);
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}
	else
		self->monsterinfo.nextframe = FRAME_run201;

	infantry_fire (self);
}

static void infantry_fire_step (edict_t *self)
{
	monster_footstep (self);
	infantry_fire (self);
}

static mframe_t infantry_frames_attack4[] = {
	{ ai_charge, 16, infantry_fire },
	{ ai_charge, 16, infantry_fire_step },
	{ ai_charge, 13, infantry_fire },
	{ ai_charge, 10, infantry_fire },
	{ ai_charge, 16, infantry_fire },
	{ ai_charge, 16, infantry_fire_step },
	{ ai_charge, 16, infantry_fire },
	{ ai_charge, 16, infantry_attack4_refire }
};
static mmove_t infantry_move_attack4 = { FRAME_run201, FRAME_run208, infantry_frames_attack4, infantry_run };

static void infantry_attack (edict_t *self)
{
	vec3_t	clear;

	monster_done_dodge (self);

	if (range_to (self, self->enemy) <= RANGE_MELEE_RR && self->monsterinfo.melee_debounce_time <= level.time)
		self->monsterinfo.currentmove = &infantry_move_attack2;
	else if (M_CheckClearShot (self, monster_flash_offset[MZ2_INFANTRY_MACHINEGUN_1], clear))
	{
		if (self->count)
			self->monsterinfo.currentmove = &infantry_move_attack1;
		else
		{
			self->monsterinfo.currentmove = random() <= 0.1 ? &infantry_move_attack5 : &infantry_move_attack3;
			self->monsterinfo.nextframe = FRAME_attak405;
		}
	}
}

static void infantry_jump_now (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 100, forward, self->velocity);
	VectorMA (self->velocity, 300, up, self->velocity);
}

static void infantry_jump2_now (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 150, forward, self->velocity);
	VectorMA (self->velocity, 400, up, self->velocity);
}

static void infantry_jump_wait_land (edict_t *self)
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

static mframe_t infantry_frames_jump[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, infantry_jump_now },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, infantry_jump_wait_land },
	{ ai_move },
	{ ai_move }
};
static mmove_t infantry_move_jump = { FRAME_jump01, FRAME_jump10, infantry_frames_jump, infantry_run };

static mframe_t infantry_frames_jump2[] = {
	{ ai_move, -8 },
	{ ai_move, -4 },
	{ ai_move, -4 },
	{ ai_move, 0, infantry_jump2_now },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, infantry_jump_wait_land },
	{ ai_move },
	{ ai_move }
};
static mmove_t infantry_move_jump2 = { FRAME_jump01, FRAME_jump10, infantry_frames_jump2, infantry_run };

static void infantry_jump (edict_t *self, int result)
{
	if (!self->enemy)
		return;

	monster_done_dodge (self);

	if (result == JUMP_UP)
		self->monsterinfo.currentmove = &infantry_move_jump2;
	else
		self->monsterinfo.currentmove = &infantry_move_jump;
}

static qboolean infantry_blocked (edict_t *self, float dist)
{
	int		result;

	if ((result = blocked_checkjump_rr (self, dist)) != JUMP_NO)
	{
		if (result != JUMP_TURN)
			infantry_jump (self, result);
		return true;
	}

	return blocked_checkplat (self, dist);
}

static qboolean infantry_duck (edict_t *self, float eta)
{
	// if we're jumping, don't dodge
	if (self->monsterinfo.currentmove == &infantry_move_jump
		|| self->monsterinfo.currentmove == &infantry_move_jump2)
		return false;

	// don't duck during our firing or melee frames
	if (self->s.frame == FRAME_attak103 || self->s.frame == FRAME_attak315
		|| self->monsterinfo.currentmove == &infantry_move_attack2)
	{
		self->monsterinfo.unduck (self);
		return false;
	}

	self->monsterinfo.currentmove = &infantry_move_duck;
	return true;
}

static qboolean infantry_sidestep (edict_t *self)
{
	// if we're jumping, don't dodge
	if (self->monsterinfo.currentmove == &infantry_move_jump
		|| self->monsterinfo.currentmove == &infantry_move_jump2)
		return false;

	if (self->monsterinfo.currentmove == &infantry_move_run)
		return true;

	// not if already sidestepping, and not unless actually shooting, or
	// already cocked
	if (self->monsterinfo.currentmove != &infantry_move_attack4
		&& (self->s.frame == FRAME_attak103 || self->s.frame == FRAME_attak311 || self->s.frame == FRAME_attak416)
		&& !self->count)
	{
		// a fire time boost so as not to fire for 1 frame
		self->monsterinfo.fire_wait += 0.3 + random() * 0.3;
		self->monsterinfo.currentmove = &infantry_move_attack4;
	}

	return true;
}

#define	SPAWNFLAG_INFANTRY_NOJUMPING	8

void SP_monster_infantry_rr (edict_t *self)
{
	self->monsterinfo.aiflags |= AI_STINKY;

	sound_pain1 = gi.soundindex ("infantry/infpain1.wav");
	sound_pain2 = gi.soundindex ("infantry/infpain2.wav");
	sound_die1 = gi.soundindex ("infantry/infdeth1.wav");
	sound_die2 = gi.soundindex ("infantry/infdeth2.wav");

	sound_gunshot = gi.soundindex ("infantry/infatck1.wav");
	sound_weapon_cock = gi.soundindex ("infantry/infatck3.wav");
	sound_punch_swing = gi.soundindex ("infantry/infatck2.wav");
	sound_punch_hit = gi.soundindex ("infantry/melee2.wav");

	sound_sight = gi.soundindex ("infantry/infsght1.wav");
	sound_search = gi.soundindex ("infantry/infsrch1.wav");
	sound_idle = gi.soundindex ("infantry/infidle1.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/infantry/tris.md2");

	gi.modelindex ("models/monsters/infantry/gibs/head.md2");
	gi.modelindex ("models/monsters/infantry/gibs/chest.md2");
	gi.modelindex ("models/monsters/infantry/gibs/gun.md2");
	gi.modelindex ("models/monsters/infantry/gibs/arm.md2");
	gi.modelindex ("models/monsters/infantry/gibs/foot.md2");

	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, 32);

	self->health = 100;
	self->gib_health = -65;
	self->mass = 200;

	self->pain = infantry_pain;
	self->die = infantry_die;

	self->monsterinfo.stand = infantry_stand;
	self->monsterinfo.walk = infantry_walk;
	self->monsterinfo.run = infantry_run;
	self->monsterinfo.dodge = (void (*)(edict_t *, edict_t *, float))M_MonsterDodge;
	self->monsterinfo.duck = infantry_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = infantry_sidestep;
	self->monsterinfo.blocked = infantry_blocked;
	self->monsterinfo.attack = infantry_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = infantry_sight;
	self->monsterinfo.idle = infantry_fidget;
	self->monsterinfo.setskin = infantry_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &infantry_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;
	self->monsterinfo.can_jump = !(self->spawnflags & SPAWNFLAG_INFANTRY_NOJUMPING);
	self->monsterinfo.drop_height = 192;
	self->monsterinfo.jump_height = 40;

	walkmonster_start (self);
}
