// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

TANK, the 2023 re-release's

Its m_tank.cpp, in C, for its own maps (RR_Map): the tank and tank
commander, whose blaster leads you, whose rockets now and then do, who
blind-fire where they last saw you, sweep the machinegun only when the
shot is clear, won't flinch from small hurts or mid-attack, lose an arm
as they fall, and gib into pieces of their own; and Quake 64's heat
seeking commander. Its frames are 1997's. m_tank.c is 1997's, for every
other map.

What it does at 40 Hz is done at this game's 10. Its guardian commander
is not drawn half as large again, as nothing here is drawn scaled, and
its heat seekers turn as The Reckoning's do.
==============================================================================
*/

#include "g_local.h"
#include "m_tank.h"

qboolean blocked_checkplat (edict_t *self, float dist);

#define SPAWNFLAG_TANK_COMMANDER_GUARDIAN		8
#define SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING	16

static int	sound_thud;
static int	sound_pain, sound_pain2;
static int	sound_idle;
static int	sound_die;
static int	sound_step;
static int	sound_sight;
static int	sound_windup;
static int	sound_strike;

static void tank_refire_rocket (edict_t *self);
static void tank_doattack_rocket (edict_t *self);
static void tank_reattack_blaster (edict_t *self);
static void tank_run (edict_t *self);
static void tank_walk (edict_t *self);
static void tank_stand (edict_t *self);
static void tank_dead (edict_t *self);
static void tank_shrink (edict_t *self);

//
// misc
//

static void tank_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void tank_footstep (edict_t *self)
{
	gi.sound (self, CHAN_BODY, sound_step, 1, ATTN_NORM, 0);
}

static void tank_thud (edict_t *self)
{
	gi.sound (self, CHAN_BODY, sound_thud, 1, ATTN_NORM, 0);
}

static void tank_windup (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_windup, 1, ATTN_NORM, 0);
}

static void tank_idle (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

//
// stand
//

static mframe_t tank_frames_stand[] = {
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
static mmove_t tank_move_stand = { FRAME_stand01, FRAME_stand30, tank_frames_stand, NULL };

static void tank_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &tank_move_stand;
}

//
// walk
//

static mframe_t tank_frames_walk[] = {
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 3 },
	{ ai_walk, 2 },
	{ ai_walk, 5 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 4, tank_footstep },
	{ ai_walk, 3 },
	{ ai_walk, 5 },
	{ ai_walk, 4 },
	{ ai_walk, 5 },
	{ ai_walk, 7 },
	{ ai_walk, 7 },
	{ ai_walk, 6 },
	{ ai_walk, 6, tank_footstep }
};
static mmove_t tank_move_walk = { FRAME_walk05, FRAME_walk20, tank_frames_walk, NULL };

static void tank_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &tank_move_walk;
}

//
// run
//

static mframe_t tank_frames_start_run[] = {
	{ ai_run },
	{ ai_run, 6 },
	{ ai_run, 6 },
	{ ai_run, 11, tank_footstep }
};
static mmove_t tank_move_start_run = { FRAME_walk01, FRAME_walk04, tank_frames_start_run, tank_run };

static mframe_t tank_frames_run[] = {
	{ ai_run, 4 },
	{ ai_run, 5 },
	{ ai_run, 3 },
	{ ai_run, 2 },
	{ ai_run, 5 },
	{ ai_run, 5 },
	{ ai_run, 4 },
	{ ai_run, 4, tank_footstep },
	{ ai_run, 3 },
	{ ai_run, 5 },
	{ ai_run, 4 },
	{ ai_run, 5 },
	{ ai_run, 7 },
	{ ai_run, 7 },
	{ ai_run, 6 },
	{ ai_run, 6, tank_footstep }
};
static mmove_t tank_move_run = { FRAME_walk05, FRAME_walk20, tank_frames_run, NULL };

static void tank_run (edict_t *self)
{
	if (self->enemy && self->enemy->client)
		self->monsterinfo.aiflags |= AI_BRUTAL;
	else
		self->monsterinfo.aiflags &= ~AI_BRUTAL;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.currentmove = &tank_move_stand;
		return;
	}

	if (self->monsterinfo.currentmove == &tank_move_walk ||
		self->monsterinfo.currentmove == &tank_move_start_run)
	{
		self->monsterinfo.currentmove = &tank_move_run;
	}
	else
	{
		self->monsterinfo.currentmove = &tank_move_start_run;
	}
}

//
// pain
//

static mframe_t tank_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t tank_move_pain1 = { FRAME_pain101, FRAME_pain104, tank_frames_pain1, tank_run };

static mframe_t tank_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t tank_move_pain2 = { FRAME_pain201, FRAME_pain205, tank_frames_pain2, tank_run };

static mframe_t tank_frames_pain3[] = {
	{ ai_move, -7 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, tank_footstep }
};
static mmove_t tank_move_pain3 = { FRAME_pain301, FRAME_pain316, tank_frames_pain3, tank_run };

static void tank_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (meansOfDeath != MOD_CHAINFIST && damage <= 10)
		return;

	if (level.time < self->pain_debounce_time)
		return;

	if (meansOfDeath != MOD_CHAINFIST)
	{
		if (damage <= 30)
			if (random() > 0.2f)
				return;

		// don't go into pain while attacking
		if ((self->s.frame >= FRAME_attak301) && (self->s.frame <= FRAME_attak330))
			return;
		if ((self->s.frame >= FRAME_attak101) && (self->s.frame <= FRAME_attak116))
			return;
	}

	self->pain_debounce_time = level.time + 3;

	if (self->count)
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	// PMM - blindfire cleanup
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
	// pmm

	if (damage <= 30)
		self->monsterinfo.currentmove = &tank_move_pain1;
	else if (damage <= 60)
		self->monsterinfo.currentmove = &tank_move_pain2;
	else
		self->monsterinfo.currentmove = &tank_move_pain3;
}

static void tank_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

// [Paril-KEX]
static qboolean M_AdjustBlindfireTarget (edict_t *self, vec3_t start, vec3_t target, vec3_t right, vec3_t out_dir)
{
	trace_t	trace;
	vec3_t	side_target;

	trace = gi.trace (start, NULL, NULL, target, self, MASK_SHOT);

	// blindfire has different fail criteria for the trace
	if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
	{
		VectorSubtract (target, start, out_dir);
		VectorNormalize (out_dir);
		return true;
	}

	// try shifting the target to the left a little (to help counter large offset)
	VectorMA (target, -20, right, side_target);
	trace = gi.trace (start, NULL, NULL, side_target, self, MASK_SHOT);

	if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
	{
		VectorSubtract (side_target, start, out_dir);
		VectorNormalize (out_dir);
		return true;
	}

	// ok, that failed.  try to the right
	VectorMA (target, 20, right, side_target);
	trace = gi.trace (start, NULL, NULL, side_target, self, MASK_SHOT);
	if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
	{
		VectorSubtract (side_target, start, out_dir);
		VectorNormalize (out_dir);
		return true;
	}

	return false;
}

//
// attacks
//

static void TankBlaster (edict_t *self)
{
	vec3_t		forward, right;
	vec3_t		start;
	vec3_t		dir;
	int			flash_number;
	qboolean	blindfire;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	blindfire = (self->monsterinfo.aiflags & AI_MANUAL_STEERING) != 0;

	if (self->s.frame == FRAME_attak110)
		flash_number = MZ2_TANK_BLASTER_1;
	else if (self->s.frame == FRAME_attak113)
		flash_number = MZ2_TANK_BLASTER_2;
	else // (self->s.frame == FRAME_attak116)
		flash_number = MZ2_TANK_BLASTER_3;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);

	// pmm - blindfire support
	if (blindfire)
	{
		if (!M_AdjustBlindfireTarget (self, start, self->monsterinfo.blind_fire_target, right, dir))
			return;
	}
	else
		PredictAim (self, self->enemy, start, 0, false, 0.f, dir, NULL);
	// pmm

	monster_fire_blaster (self, start, dir, 30, 800, flash_number, EF_BLASTER);
}

static void TankStrike (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_strike, 1, ATTN_NORM, 0);
}

static void TankRocket (edict_t *self)
{
	vec3_t		forward, right;
	vec3_t		start;
	vec3_t		dir;
	vec3_t		vec;
	int			flash_number;
	int			rocketSpeed; // PGM
	qboolean	blindfire;
	trace_t		trace;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	blindfire = (self->monsterinfo.aiflags & AI_MANUAL_STEERING) != 0;

	if (self->s.frame == FRAME_attak324)
		flash_number = MZ2_TANK_ROCKET_1;
	else if (self->s.frame == FRAME_attak327)
		flash_number = MZ2_TANK_ROCKET_2;
	else // (self->s.frame == FRAME_attak330)
		flash_number = MZ2_TANK_ROCKET_3;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);

	if (self->speed)
		rocketSpeed = self->speed;
	else if (self->spawnflags & SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING)
		rocketSpeed = 500;
	else
		rocketSpeed = 650;

	//  PMM - blindfire shooting
	if (blindfire)
	{
		VectorCopy (self->monsterinfo.blind_fire_target, vec);
		VectorSubtract (vec, start, dir);
	}
	// pmm
	// don't shoot at feet if they're above me.
	else if (random() < 0.66f || (start[2] < self->enemy->absmin[2]))
	{
		VectorCopy (self->enemy->s.origin, vec);
		vec[2] += self->enemy->viewheight;
		VectorSubtract (vec, start, dir);
	}
	else
	{
		VectorCopy (self->enemy->s.origin, vec);
		vec[2] = self->enemy->absmin[2] + 1;
		VectorSubtract (vec, start, dir);
	}

	//======
	// PMM - lead target  (not when blindfiring)
	// 20, 35, 50, 65 chance of leading
	if ((!blindfire) && ((random() < (0.2f + ((3 - (int) skill->value) * 0.15f)))))
		PredictAim (self, self->enemy, start, rocketSpeed, false, 0, dir, vec);
	// PMM - lead target
	//======

	VectorNormalize (dir);

	// pmm blindfire doesn't check target (done in checkattack)
	// paranoia, make sure we're not shooting a target right next to us
	if (blindfire)
	{
		// blindfire has different fail criteria for the trace
		if (M_AdjustBlindfireTarget (self, start, vec, right, dir))
		{
			if (self->spawnflags & SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING)
				monster_fire_heat (self, start, dir, 50, rocketSpeed, flash_number);
			else
				monster_fire_rocket (self, start, dir, 50, rocketSpeed, flash_number);
		}
	}
	else
	{
		trace = gi.trace (start, NULL, NULL, vec, self, MASK_SHOT);

		if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
		{
			if (self->spawnflags & SPAWNFLAG_TANK_COMMANDER_HEAT_SEEKING)
				monster_fire_heat (self, start, dir, 50, rocketSpeed, flash_number);
			else
				monster_fire_rocket (self, start, dir, 50, rocketSpeed, flash_number);
		}
	}
}

static void TankMachineGun (edict_t *self)
{
	vec3_t	dir;
	vec3_t	vec;
	vec3_t	start;
	vec3_t	forward, right;
	int		flash_number;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	flash_number = MZ2_TANK_MACHINEGUN_1 + (self->s.frame - FRAME_attak406);

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_number], forward, right, start);

	VectorCopy (self->enemy->s.origin, vec);
	vec[2] += self->enemy->viewheight;
	VectorSubtract (vec, start, vec);
	vectoangles (vec, vec);
	dir[0] = vec[0];

	if (self->s.frame <= FRAME_attak415)
		dir[1] = self->s.angles[1] - 8 * (self->s.frame - FRAME_attak411);
	else
		dir[1] = self->s.angles[1] + 8 * (self->s.frame - FRAME_attak419);
	dir[2] = 0;

	AngleVectors (dir, forward, NULL, NULL);

	monster_fire_bullet (self, start, forward, 20, 4, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, flash_number);
}

static void tank_blind_check (edict_t *self)
{
	vec3_t	aim;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		VectorSubtract (self->monsterinfo.blind_fire_target, self->s.origin, aim);
		self->ideal_yaw = vectoyaw (aim);
	}
}

static mframe_t tank_frames_attack_blast[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -1 },
	{ ai_charge, -2 },
	{ ai_charge, -1 },
	{ ai_charge, -1, tank_blind_check },
	{ ai_charge },
	{ ai_charge, 0, TankBlaster }, // 10
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankBlaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankBlaster } // 16
};
static mmove_t tank_move_attack_blast = { FRAME_attak101, FRAME_attak116, tank_frames_attack_blast, tank_reattack_blaster };

static mframe_t tank_frames_reattack_blast[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankBlaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankBlaster } // 16
};
static mmove_t tank_move_reattack_blast = { FRAME_attak111, FRAME_attak116, tank_frames_reattack_blast, tank_reattack_blaster };

static mframe_t tank_frames_attack_post_blast[] = {
	{ ai_move }, // 17
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, -2, tank_footstep } // 22
};
static mmove_t tank_move_attack_post_blast = { FRAME_attak117, FRAME_attak122, tank_frames_attack_post_blast, tank_run };

static void tank_reattack_blaster (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		self->monsterinfo.currentmove = &tank_move_attack_post_blast;
		return;
	}

	if (visible (self, self->enemy))
		if (self->enemy->health > 0)
			if (random() <= 0.6f)
			{
				self->monsterinfo.currentmove = &tank_move_reattack_blast;
				return;
			}
	self->monsterinfo.currentmove = &tank_move_attack_post_blast;
}

static void tank_poststrike (edict_t *self)
{
	self->enemy = NULL;
	// [Paril-KEX]
	self->monsterinfo.pausetime = 100000000;
	self->monsterinfo.stand (self);
}

static mframe_t tank_frames_attack_strike[] = {
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 6 },
	{ ai_move, 7 },
	{ ai_move, 9, tank_footstep },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move, 2, tank_footstep },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -2 },
	{ ai_move, -2 },
	{ ai_move, 0, tank_windup },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, TankStrike },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -3 },
	{ ai_move, -10 },
	{ ai_move, -10 },
	{ ai_move, -2 },
	{ ai_move, -3 },
	{ ai_move, -2, tank_footstep }
};
static mmove_t tank_move_attack_strike = { FRAME_attak201, FRAME_attak238, tank_frames_attack_strike, tank_poststrike };

static mframe_t tank_frames_attack_pre_rocket[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 10

	{ ai_charge },
	{ ai_charge, 1 },
	{ ai_charge, 2 },
	{ ai_charge, 7 },
	{ ai_charge, 7 },
	{ ai_charge, 7, tank_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 20

	{ ai_charge, -3 }
};
static mmove_t tank_move_attack_pre_rocket = { FRAME_attak301, FRAME_attak321, tank_frames_attack_pre_rocket, tank_doattack_rocket };

static mframe_t tank_frames_attack_fire_rocket[] = {
	{ ai_charge, -3, tank_blind_check }, // Loop Start	22
	{ ai_charge },
	{ ai_charge, 0, TankRocket }, // 24
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, TankRocket },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, -1, TankRocket } // 30	Loop End
};
static mmove_t tank_move_attack_fire_rocket = { FRAME_attak322, FRAME_attak330, tank_frames_attack_fire_rocket, tank_refire_rocket };

static mframe_t tank_frames_attack_post_rocket[] = {
	{ ai_charge }, // 31
	{ ai_charge, -1 },
	{ ai_charge, -1 },
	{ ai_charge },
	{ ai_charge, 2 },
	{ ai_charge, 3 },
	{ ai_charge, 4 },
	{ ai_charge, 2 },
	{ ai_charge },
	{ ai_charge }, // 40

	{ ai_charge },
	{ ai_charge, -9 },
	{ ai_charge, -8 },
	{ ai_charge, -7 },
	{ ai_charge, -1 },
	{ ai_charge, -1, tank_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }, // 50

	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t tank_move_attack_post_rocket = { FRAME_attak331, FRAME_attak353, tank_frames_attack_post_rocket, tank_run };

static mframe_t tank_frames_attack_chain[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ NULL, 0, TankMachineGun },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t tank_move_attack_chain = { FRAME_attak401, FRAME_attak429, tank_frames_attack_chain, tank_run };

static void tank_refire_rocket (edict_t *self)
{
	// PMM - blindfire cleanup
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		self->monsterinfo.currentmove = &tank_move_attack_post_rocket;
		return;
	}
	// pmm

	if (self->enemy->health > 0)
		if (visible (self, self->enemy))
			if (random() <= 0.4f)
			{
				self->monsterinfo.currentmove = &tank_move_attack_fire_rocket;
				return;
			}
	self->monsterinfo.currentmove = &tank_move_attack_post_rocket;
}

static void tank_doattack_rocket (edict_t *self)
{
	self->monsterinfo.currentmove = &tank_move_attack_fire_rocket;
}

// not at a tesla mine
static qboolean tank_can_machinegun (edict_t *self, qboolean mines)
{
	vec3_t	clear;

	if (mines && self->enemy->classname && !strcmp (self->enemy->classname, "tesla_mine"))
		return false;
	return M_CheckClearShot (self, monster_flash_offset[MZ2_TANK_MACHINEGUN_5], clear);
}

static void tank_attack (edict_t *self)
{
	vec3_t		vec, clear;
	float		range;
	float		r;
	// PMM
	float		chance;
	qboolean	rocket_visible, blaster_visible, use_rocket;

	if (!self->enemy || !self->enemy->inuse)
		return;

	if (self->enemy->health <= 0)
	{
		self->monsterinfo.currentmove = &tank_move_attack_strike;
		self->monsterinfo.aiflags &= ~AI_BRUTAL;
		return;
	}

	// PMM
	if (self->monsterinfo.attack_state == AS_BLIND)
	{
		// setup shot probabilities
		if (self->monsterinfo.blind_fire_delay < 1.0)
			chance = 1.0f;
		else if (self->monsterinfo.blind_fire_delay < 7.5)
			chance = 0.4f;
		else
			chance = 0.1f;

		r = random();

		self->monsterinfo.blind_fire_delay += 5.2 + 3.0 * random();

		// don't shoot at the origin
		if (VectorCompare (self->monsterinfo.blind_fire_target, vec3_origin))
			return;

		// don't shoot if the dice say not to
		if (r > chance)
			return;

		rocket_visible = M_CheckClearShot (self, monster_flash_offset[MZ2_TANK_ROCKET_1], clear);
		blaster_visible = M_CheckClearShot (self, monster_flash_offset[MZ2_TANK_BLASTER_1], clear);

		if (!rocket_visible && !blaster_visible)
			return;

		use_rocket = (rocket_visible && blaster_visible) ? (rand () & 1) : rocket_visible;

		// turn on manual steering to signal both manual steering and blindfire
		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;

		if (use_rocket)
			self->monsterinfo.currentmove = &tank_move_attack_fire_rocket;
		else
		{
			self->monsterinfo.currentmove = &tank_move_attack_blast;
			self->monsterinfo.nextframe = FRAME_attak108;
		}

		self->monsterinfo.attack_finished = level.time + 3 + 2 * random();
		self->pain_debounce_time = level.time + 5; // no pain for a while
		return;
	}
	// pmm

	VectorSubtract (self->enemy->s.origin, self->s.origin, vec);
	range = VectorLength (vec);

	r = random();

	if (range <= 125)
	{
		if (tank_can_machinegun (self, true) && r < 0.5f)
			self->monsterinfo.currentmove = &tank_move_attack_chain;
		else if (M_CheckClearShot (self, monster_flash_offset[MZ2_TANK_BLASTER_1], clear))
			self->monsterinfo.currentmove = &tank_move_attack_blast;
	}
	else if (range <= 250)
	{
		if (tank_can_machinegun (self, true) && r < 0.25f)
			self->monsterinfo.currentmove = &tank_move_attack_chain;
		else if (M_CheckClearShot (self, monster_flash_offset[MZ2_TANK_BLASTER_1], clear))
			self->monsterinfo.currentmove = &tank_move_attack_blast;
	}
	else
	{
		qboolean can_machinegun = tank_can_machinegun (self, false);
		qboolean can_rocket = M_CheckClearShot (self, monster_flash_offset[MZ2_TANK_ROCKET_1], clear);

		if (can_machinegun && r < 0.33f)
			self->monsterinfo.currentmove = &tank_move_attack_chain;
		else if (can_rocket && r < 0.66f)
		{
			self->monsterinfo.currentmove = &tank_move_attack_pre_rocket;
			self->pain_debounce_time = level.time + 5; // no pain for a while
		}
		else if (M_CheckClearShot (self, monster_flash_offset[MZ2_TANK_BLASTER_1], clear))
			self->monsterinfo.currentmove = &tank_move_attack_blast;
	}
}

//
// death
//

static void tank_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -16);
	VectorSet (self->maxs, 16, 16, -0);
	monster_dead (self);
}

static void tank_shrink (edict_t *self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static mframe_t tank_frames_death1[] = {
	{ ai_move, -7 },
	{ ai_move, -2 },
	{ ai_move, -2 },
	{ ai_move, 1 },
	{ ai_move, 3 },
	{ ai_move, 6 },
	{ ai_move, 1 },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -2 },
	{ ai_move },
	{ ai_move },
	{ ai_move, -3 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -4 },
	{ ai_move, -6 },
	{ ai_move, -4 },
	{ ai_move, -5 },
	{ ai_move, -7, tank_shrink },
	{ ai_move, -15, tank_thud },
	{ ai_move, -5 },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t tank_move_death = { FRAME_death101, FRAME_death132, tank_frames_death1, tank_dead };

static gib_def_t tank_gibs[] = {
	{ 1, "models/objects/gibs/sm_meat/tris.md2" },
	{ 3, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
	{ 1, "models/objects/gibs/gear/tris.md2", GIB_METALLIC },
	{ 2, "models/monsters/tank/gibs/foot.md2", GIB_SKINNED | GIB_METALLIC },
	{ 2, "models/monsters/tank/gibs/thigh.md2", GIB_SKINNED | GIB_METALLIC },
	{ 1, "models/monsters/tank/gibs/chest.md2", GIB_SKINNED },
	{ 1, "models/monsters/tank/gibs/head.md2", GIB_HEAD | GIB_SKINNED },
	{ 0, NULL }
};

static void tank_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	edict_t	*arm_gib;
	vec3_t	fwd, rgt, up;

	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs (self, damage, tank_gibs);

		if (!self->style)
		{
			arm_gib = ThrowGib (self, "models/monsters/tank/gibs/barm.md2", damage, GIB_ORGANIC);
			if (arm_gib)
				arm_gib->s.skinnum = self->s.skinnum;
		}

		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// [Paril-KEX] dropped arm
	if (!self->style)
	{
		self->style = 1;

		AngleVectors (self->s.angles, fwd, rgt, up);

		arm_gib = ThrowGib (self, "models/monsters/tank/gibs/barm.md2", damage, GIB_ORGANIC);
		if (arm_gib)
		{
			VectorMA (self->s.origin, -16.f, rgt, arm_gib->s.origin);
			VectorMA (arm_gib->s.origin, 23.f, up, arm_gib->s.origin);
			VectorCopy (arm_gib->s.origin, arm_gib->s.old_origin);
			VectorSet (arm_gib->avelocity, crandom() * 15.f, crandom() * 15.f, 180.f);
			VectorScale (up, 100.f, arm_gib->velocity);
			VectorMA (arm_gib->velocity, -120.f, rgt, arm_gib->velocity);
			VectorCopy (self->s.angles, arm_gib->s.angles);
			arm_gib->s.angles[2] = -90.f;
			arm_gib->s.skinnum = self->s.skinnum / 2;
			gi.linkentity (arm_gib);
		}
	}

	// regular death
	gi.sound (self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	self->monsterinfo.currentmove = &tank_move_death;
}

//===========
// PGM
static qboolean tank_blocked (edict_t *self, float dist)
{
	return blocked_checkplat (self, dist);
}
// PGM
//===========

//
// monster_tank
//

void SP_monster_tank_rr (edict_t *self)
{
	self->s.modelindex = gi.modelindex ("models/monsters/tank/tris.md2");
	VectorSet (self->mins, -32, -32, -16);
	VectorSet (self->maxs, 32, 32, 64);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	gi.modelindex ("models/monsters/tank/gibs/barm.md2");
	gi.modelindex ("models/monsters/tank/gibs/head.md2");
	gi.modelindex ("models/monsters/tank/gibs/chest.md2");
	gi.modelindex ("models/monsters/tank/gibs/foot.md2");
	gi.modelindex ("models/monsters/tank/gibs/thigh.md2");

	sound_thud = gi.soundindex ("tank/tnkdeth2.wav");
	sound_idle = gi.soundindex ("tank/tnkidle1.wav");
	sound_die = gi.soundindex ("tank/death.wav");
	sound_step = gi.soundindex ("tank/step.wav");
	sound_windup = gi.soundindex ("tank/tnkatck4.wav");
	sound_strike = gi.soundindex ("tank/tnkatck5.wav");
	sound_sight = gi.soundindex ("tank/sight1.wav");

	gi.soundindex ("tank/tnkatck1.wav");
	gi.soundindex ("tank/tnkatk2a.wav");
	gi.soundindex ("tank/tnkatk2b.wav");
	gi.soundindex ("tank/tnkatk2c.wav");
	gi.soundindex ("tank/tnkatk2d.wav");
	gi.soundindex ("tank/tnkatk2e.wav");
	gi.soundindex ("tank/tnkatck3.wav");

	if (strcmp (self->classname, "monster_tank_commander") == 0)
	{
		self->health = 1000;
		self->gib_health = -225;
		self->count = 1;
		sound_pain2 = gi.soundindex ("tank/pain.wav");
	}
	else
	{
		self->health = 750;
		self->gib_health = -200;
		sound_pain = gi.soundindex ("tank/tnkpain2.wav");
	}

	self->monsterinfo.scale = MODEL_SCALE;

	// [Paril-KEX] N64 tank commander is a chonky boy
	if (self->spawnflags & SPAWNFLAG_TANK_COMMANDER_GUARDIAN)
	{
		if (!self->scale)
			self->scale = 1.5;		// monster_start makes its box so
		self->health = 1500;
	}

	self->mass = 500;

	self->pain = tank_pain;
	self->die = tank_die;
	self->monsterinfo.stand = tank_stand;
	self->monsterinfo.walk = tank_walk;
	self->monsterinfo.run = tank_run;
	self->monsterinfo.dodge = NULL;
	self->monsterinfo.attack = tank_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = tank_sight;
	self->monsterinfo.idle = tank_idle;
	self->monsterinfo.blocked = tank_blocked; // PGM
	self->monsterinfo.setskin = tank_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &tank_move_stand;

	walkmonster_start (self);

	// PMM
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
	self->monsterinfo.blindfire = true;
	// pmm

	if (strcmp (self->classname, "monster_tank_commander") == 0)
		self->s.skinnum = 2;
}
