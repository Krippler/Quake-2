// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

SOLDIER, the 2023 re-release's

Its m_soldier.cpp, in C, for its own maps (RR_Map), where its model has
the frames this needs: the light, machinegun and shotgun guards and The
Reckoning's ripper, hypergun and laser ones, running as they fire,
diving and firing prone, ducking and sidestepping, blind firing where
they last saw the enemy, the shotgun guard cocking between shots, and
gibbing into pieces of their own. m_soldier.c is 1997's, for every
other map.

What it does at 40 Hz is done at this game's 10; it has no N64 cutscene
walk; its laser guard's beam is The Reckoning's (soldierh_laserbeam).
==============================================================================
*/

#include "g_local.h"
#include "m_soldier_rr.h"

void soldierh_laserbeam (edict_t *self, int flash_index);
void M_MonsterDodge (edict_t *self, edict_t *attacker, float eta, trace_t *tr);
qboolean blocked_checkplat (edict_t *self, float dist);

static int	sound_idle;
static int	sound_sight1;
static int	sound_sight2;
static int	sound_pain_light;
static int	sound_pain;
static int	sound_pain_ss;
static int	sound_death_light;
static int	sound_death;
static int	sound_death_ss;
static int	sound_cock;

static void soldier_stand (edict_t *self);
static void soldier_run (edict_t *self);
static void soldier_blind (edict_t *self);
static void soldier_dead (edict_t *self);
static void soldier_stand_up (edict_t *self);
static mmove_t soldier_move_trip;
static mmove_t soldier_move_attack5;

static void soldier_start_charge (edict_t *self)
{
	self->monsterinfo.aiflags |= AI_CHARGING;
}

static void soldier_stop_charge (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_CHARGING;
}

static void soldier_idle (edict_t *self)
{
	if (random() > 0.8)
		gi.sound (self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE, 0);
}

static void soldier_cock (edict_t *self)
{
	if (self->s.frame == FRAME_stand322)
		gi.sound (self, CHAN_WEAPON, sound_cock, 1, ATTN_IDLE, 0);
	else
		gi.sound (self, CHAN_WEAPON, sound_cock, 1, ATTN_NORM, 0);

	// reset cockness
	self->dmg = 0;
}

// The Reckoning's hypergun guard's loop, as it fires
static void soldierh_hyper_laser_sound_start (edict_t *self)
{
	if (self->style == 1 && self->count >= 2 && self->count < 4)
		self->s.sound = gi.soundindex ("weapons/hyprbl1a.wav");
}

static void soldierh_hyper_laser_sound_end (edict_t *self)
{
	if (self->s.sound)
	{
		if (self->count >= 2 && self->count < 4)
			gi.sound (self, CHAN_AUTO, gi.soundindex ("weapons/hyprbd1a.wav"), 1, ATTN_NORM, 0);
		self->s.sound = 0;
	}
}

// STAND

static mframe_t soldier_frames_stand1[] = {
	{ ai_stand, 0, soldier_idle },
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
static mmove_t soldier_move_stand1 = { FRAME_stand101, FRAME_stand130, soldier_frames_stand1, soldier_stand };

static mframe_t soldier_frames_stand2[] = {
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
	{ ai_stand, 0, monster_footstep },
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
	{ ai_stand, 0, monster_footstep }
};
static mmove_t soldier_move_stand2 = { FRAME_stand201, FRAME_stand240, soldier_frames_stand2, soldier_stand };

static mframe_t soldier_frames_stand3[] = {
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
	{ ai_stand, 0, soldier_cock },
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
static mmove_t soldier_move_stand3 = { FRAME_stand301, FRAME_stand339, soldier_frames_stand3, soldier_stand };

static void soldier_stand (edict_t *self)
{
	float	r = random();

	if (self->monsterinfo.currentmove != &soldier_move_stand1 || r < 0.6)
		self->monsterinfo.currentmove = &soldier_move_stand1;
	else if (r < 0.8)
		self->monsterinfo.currentmove = &soldier_move_stand2;
	else
		self->monsterinfo.currentmove = &soldier_move_stand3;
	soldierh_hyper_laser_sound_end (self);
}

// WALK

static void soldier_walk1_random (edict_t *self)
{
	if (random() > 0.1)
		self->monsterinfo.nextframe = FRAME_walk101;
}

static mframe_t soldier_frames_walk1[] = {
	{ ai_walk, 3 },
	{ ai_walk, 6 },
	{ ai_walk, 2 },
	{ ai_walk, 2, monster_footstep },
	{ ai_walk, 2 },
	{ ai_walk, 1 },
	{ ai_walk, 6 },
	{ ai_walk, 5 },
	{ ai_walk, 3, monster_footstep },
	{ ai_walk, -1, soldier_walk1_random },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk }
};
static mmove_t soldier_move_walk1 = { FRAME_walk101, FRAME_walk133, soldier_frames_walk1, NULL };

static mframe_t soldier_frames_walk2[] = {
	{ ai_walk, 4, monster_footstep },
	{ ai_walk, 4 },
	{ ai_walk, 9 },
	{ ai_walk, 8 },
	{ ai_walk, 5 },
	{ ai_walk, 1, monster_footstep },
	{ ai_walk, 3 },
	{ ai_walk, 7 },
	{ ai_walk, 6 },
	{ ai_walk, 7 }
};
static mmove_t soldier_move_walk2 = { FRAME_walk209, FRAME_walk218, soldier_frames_walk2, NULL };

static void soldier_walk (edict_t *self)
{
	if (random() < 0.5)
		self->monsterinfo.currentmove = &soldier_move_walk1;
	else
		self->monsterinfo.currentmove = &soldier_move_walk2;
}

// RUN

static void soldier_run_step (edict_t *self)
{
	monster_done_dodge (self);
	monster_footstep (self);
}

static mframe_t soldier_frames_start_run[] = {
	{ ai_run, 7 },
	{ ai_run, 5 }
};
static mmove_t soldier_move_start_run = { FRAME_run01, FRAME_run02, soldier_frames_start_run, soldier_run };

static mframe_t soldier_frames_run[] = {
	{ ai_run, 10 },
	{ ai_run, 11, soldier_run_step },
	{ ai_run, 11 },
	{ ai_run, 16 },
	{ ai_run, 10, monster_footstep },
	{ ai_run, 15, monster_done_dodge }
};
static mmove_t soldier_move_run = { FRAME_run03, FRAME_run08, soldier_frames_run, NULL };

static void soldier_run (edict_t *self)
{
	monster_done_dodge (self);
	soldierh_hyper_laser_sound_end (self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.currentmove = &soldier_move_stand1;
		return;
	}

	if (self->monsterinfo.currentmove == &soldier_move_walk1
		|| self->monsterinfo.currentmove == &soldier_move_walk2
		|| self->monsterinfo.currentmove == &soldier_move_start_run
		|| self->monsterinfo.currentmove == &soldier_move_run)
		self->monsterinfo.currentmove = &soldier_move_run;
	else
		self->monsterinfo.currentmove = &soldier_move_start_run;
}

// PAIN

static mframe_t soldier_frames_pain1[] = {
	{ ai_move, -3 },
	{ ai_move, 4 },
	{ ai_move, 1 },
	{ ai_move, 1 },
	{ ai_move }
};
static mmove_t soldier_move_pain1 = { FRAME_pain101, FRAME_pain105, soldier_frames_pain1, soldier_run };

static mframe_t soldier_frames_pain2[] = {
	{ ai_move, -13 },
	{ ai_move, -1 },
	{ ai_move, 2 },
	{ ai_move, 4 },
	{ ai_move, 2 },
	{ ai_move, 3 },
	{ ai_move, 2 }
};
static mmove_t soldier_move_pain2 = { FRAME_pain201, FRAME_pain207, soldier_frames_pain2, soldier_run };

static mframe_t soldier_frames_pain3[] = {
	{ ai_move, -8 },
	{ ai_move, 10 },
	{ ai_move, -4, monster_footstep },
	{ ai_move, -1 },
	{ ai_move, -3 },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move, 4 },
	{ ai_move, 3 },
	{ ai_move, 2, monster_footstep }
};
static mmove_t soldier_move_pain3 = { FRAME_pain301, FRAME_pain318, soldier_frames_pain3, soldier_run };

static mframe_t soldier_frames_pain4[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -10 },
	{ ai_move, -6 },
	{ ai_move, 8 },
	{ ai_move, 4 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 5 },
	{ ai_move, 2 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move }
};
static mmove_t soldier_move_pain4 = { FRAME_pain401, FRAME_pain417, soldier_frames_pain4, soldier_run };

static void soldier_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	float	r;
	int		n;

	monster_done_dodge (self);
	soldier_stop_charge (self);

	// if we're blind firing, this needs to be turned off here
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

	if (level.time < self->pain_debounce_time)
	{
		if (self->velocity[2] > 100 && (self->monsterinfo.currentmove == &soldier_move_pain1
			|| self->monsterinfo.currentmove == &soldier_move_pain2
			|| self->monsterinfo.currentmove == &soldier_move_pain3))
		{
			if (self->monsterinfo.aiflags & AI_DUCKED)
				monster_duck_up (self);
			self->monsterinfo.currentmove = &soldier_move_pain4;
			soldierh_hyper_laser_sound_end (self);
		}
		return;
	}

	self->pain_debounce_time = level.time + 3;

	n = self->count | 1;
	if (n == 1)
		gi.sound (self, CHAN_VOICE, sound_pain_light, 1, ATTN_NORM, 0);
	else if (n == 3)
		gi.sound (self, CHAN_VOICE, sound_pain, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain_ss, 1, ATTN_NORM, 0);

	if (self->velocity[2] > 100)
	{
		if (self->monsterinfo.aiflags & AI_DUCKED)
			monster_duck_up (self);
		self->monsterinfo.currentmove = &soldier_move_pain4;
		soldierh_hyper_laser_sound_end (self);
		return;
	}

	if (!M_ShouldReactToPain (self))
		return;		// no pain anims in nightmare

	r = random();
	if (r < 0.33)
		self->monsterinfo.currentmove = &soldier_move_pain1;
	else if (r < 0.66)
		self->monsterinfo.currentmove = &soldier_move_pain2;
	else
		self->monsterinfo.currentmove = &soldier_move_pain3;

	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up (self);
	soldierh_hyper_laser_sound_end (self);
}

static void soldier_setskin (edict_t *self)
{
	if (self->health < self->max_health / 2)
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

// ATTACK

static int blaster_flash[] = {MZ2_SOLDIER_BLASTER_1, MZ2_SOLDIER_BLASTER_2, MZ2_SOLDIER_BLASTER_3, MZ2_SOLDIER_BLASTER_4, MZ2_SOLDIER_BLASTER_5, MZ2_SOLDIER_BLASTER_6, MZ2_SOLDIER_BLASTER_7, MZ2_SOLDIER_BLASTER_8, MZ2_SOLDIER_BLASTER_9};
static int shotgun_flash[] = {MZ2_SOLDIER_SHOTGUN_1, MZ2_SOLDIER_SHOTGUN_2, MZ2_SOLDIER_SHOTGUN_3, MZ2_SOLDIER_SHOTGUN_4, MZ2_SOLDIER_SHOTGUN_5, MZ2_SOLDIER_SHOTGUN_6, MZ2_SOLDIER_SHOTGUN_7, MZ2_SOLDIER_SHOTGUN_8, MZ2_SOLDIER_SHOTGUN_9};
static int machinegun_flash[] = {MZ2_SOLDIER_MACHINEGUN_1, MZ2_SOLDIER_MACHINEGUN_2, MZ2_SOLDIER_MACHINEGUN_3, MZ2_SOLDIER_MACHINEGUN_4, MZ2_SOLDIER_MACHINEGUN_5, MZ2_SOLDIER_MACHINEGUN_6, MZ2_SOLDIER_MACHINEGUN_7, MZ2_SOLDIER_MACHINEGUN_8, MZ2_SOLDIER_MACHINEGUN_9};
static int ripper_flash[] = {MZ2_SOLDIER_RIPPER_1, MZ2_SOLDIER_RIPPER_2, MZ2_SOLDIER_RIPPER_3, MZ2_SOLDIER_RIPPER_4, MZ2_SOLDIER_RIPPER_5, MZ2_SOLDIER_RIPPER_6, MZ2_SOLDIER_RIPPER_7, MZ2_SOLDIER_RIPPER_8, MZ2_SOLDIER_RIPPER_9};
static int hyper_flash[] = {MZ2_SOLDIER_HYPERGUN_1, MZ2_SOLDIER_HYPERGUN_2, MZ2_SOLDIER_HYPERGUN_3, MZ2_SOLDIER_HYPERGUN_4, MZ2_SOLDIER_HYPERGUN_5, MZ2_SOLDIER_HYPERGUN_6, MZ2_SOLDIER_HYPERGUN_7, MZ2_SOLDIER_HYPERGUN_8, MZ2_SOLDIER_HYPERGUN_9};

// whether a held frame of machinegun fire goes on
static void soldier_hold_fire (edict_t *self)
{
	if (level.time >= self->monsterinfo.fire_wait)
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
	else
		self->monsterinfo.aiflags |= AI_HOLD_FRAME;
}

/*
=============
soldier_fire

Both the 1997 guards' and The Reckoning's: from the gun, at the enemy or
where it was last seen when blind, spread wide; a dead one's at nothing
in particular; and, angle_limited, not when the enemy is too far round
from where he is running or lying.
=============
*/
static void soldier_fire (edict_t *self, int flash_number, qboolean angle_limited)
{
	vec3_t	start, forward, right, up, aim, dir, end;
	float	r, u, spread_r, spread_u;
	int		flash_index;

	if (self->style == 1)
	{
		if (self->count < 2)
			flash_index = ripper_flash[flash_number];
		else if (self->count < 4)
			flash_index = hyper_flash[flash_number];
		else
			flash_index = machinegun_flash[flash_number];
		spread_r = 100;
		spread_u = 50;
	}
	else
	{
		if (self->count < 2)
			flash_index = blaster_flash[flash_number];
		else if (self->count < 4)
			flash_index = shotgun_flash[flash_number];
		else
			flash_index = machinegun_flash[flash_number];
		spread_r = 1000;
		spread_u = 500;
	}

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flash_index], forward, right, start);

	if (flash_number == 5 || flash_number == 6)		// he's dead
		VectorCopy (forward, aim);
	else
	{
		if (!self->enemy || !self->enemy->inuse)
		{
			self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
			return;
		}

		if (self->monsterinfo.attack_state == AS_BLIND)
			VectorCopy (self->monsterinfo.blind_fire_target, end);
		else
			VectorCopy (self->enemy->s.origin, end);
		end[2] += self->enemy->viewheight;
		VectorSubtract (end, start, aim);

		if (angle_limited)
		{
			VectorCopy (aim, dir);
			VectorNormalize (dir);
			if (DotProduct (dir, forward) < 0.5)	// ~25 degree angle
			{
				soldier_hold_fire (self);
				return;
			}
		}

		vectoangles (aim, dir);
		AngleVectors (dir, forward, right, up);

		r = crandom() * spread_r;
		u = crandom() * spread_u;
		VectorMA (start, 8192, forward, end);
		VectorMA (end, r, right, end);
		VectorMA (end, u, up, end);

		VectorSubtract (end, start, aim);
		VectorNormalize (aim);
	}

	if (self->style == 1)
	{
		if (self->count <= 1)
			monster_fire_ionripper (self, start, aim, 5, 600, flash_index, EF_IONRIPPER);
		else if (self->count <= 3)
			monster_fire_blueblaster (self, start, aim, 1, 600, flash_index, EF_BLUEHYPERBLASTER);
		else
		{
			if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
				self->monsterinfo.fire_wait = level.time + 0.3 + random() * 0.8;
			soldierh_laserbeam (self, flash_index);
			soldier_hold_fire (self);
		}
		return;
	}

	if (self->count <= 1)
		monster_fire_blaster (self, start, aim, 5, 600, flash_index, EF_BLASTER);
	else if (self->count <= 3)
	{
		monster_fire_shotgun (self, start, aim, 2, 1, 1500, 750, 9, flash_index);
		// he must cock
		self->dmg = 1;
	}
	else
	{
		if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
			self->monsterinfo.fire_wait = level.time + 0.3 + random() * 0.8;
		monster_fire_bullet (self, start, aim, 2, 4, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, flash_index);
		soldier_hold_fire (self);
	}
}

// ATTACK1 (blaster/shotgun)

static void soldier_fire1 (edict_t *self)
{
	soldier_fire (self, 0, false);
}

static void soldier_attack1_refire1 (edict_t *self)
{
	if (self->count <= 0)
		self->monsterinfo.nextframe = FRAME_attak110;

	// blindfire
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		return;
	}

	if (!self->enemy || self->count > 1 || self->enemy->health <= 0)
		return;

	if ((random() < 0.5 && visible (self, self->enemy)) || range_to (self, self->enemy) <= RANGE_MELEE_RR)
		self->monsterinfo.nextframe = FRAME_attak102;
	else
		self->monsterinfo.nextframe = FRAME_attak110;
}

static void soldier_attack1_refire2 (edict_t *self)
{
	if (!self->enemy || self->count < 2 || self->enemy->health <= 0)
		return;

	if (((self->radius_dmg || random() < 0.5) && visible (self, self->enemy)) || range_to (self, self->enemy) <= RANGE_MELEE_RR)
	{
		self->monsterinfo.nextframe = FRAME_attak102;
		self->radius_dmg = 0;
	}
}

static void soldier_attack1_shotgun_check (edict_t *self)
{
	if (self->dmg)
	{
		self->monsterinfo.nextframe = FRAME_attak106;
		// force a refire
		self->radius_dmg = 1;
	}
}

static void soldier_blind_check (edict_t *self)
{
	vec3_t	aim;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		VectorSubtract (self->monsterinfo.blind_fire_target, self->s.origin, aim);
		self->ideal_yaw = vectoyaw (aim);
	}
}

static mframe_t soldier_frames_attack1[] = {
	{ ai_charge, 0, soldier_blind_check },
	{ ai_charge, 0, soldier_attack1_shotgun_check },
	{ ai_charge, 0, soldier_fire1 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, soldier_attack1_refire1 },
	{ ai_charge },
	{ ai_charge, 0, soldier_cock },
	{ ai_charge, 0, soldier_attack1_refire2 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t soldier_move_attack1 = { FRAME_attak101, FRAME_attak112, soldier_frames_attack1, soldier_run };

static void soldierh_hyper_refire1 (edict_t *self)
{
	if (!self->enemy)
		return;
	if (self->count >= 2 && self->count < 4 && random() < 0.7 && visible (self, self->enemy))
		self->s.frame = FRAME_attak103;
}

static void soldierh_hyperripper1 (edict_t *self)
{
	if (self->count < 4)
		soldier_fire (self, 0, false);
}

static mframe_t soldierh_frames_attack1[] = {
	{ ai_charge, 0, soldier_blind_check },
	{ ai_charge, 0, soldierh_hyper_laser_sound_start },
	{ ai_charge, 0, soldier_fire1 },
	{ ai_charge, 0, soldierh_hyperripper1 },
	{ ai_charge, 0, soldierh_hyperripper1 },
	{ ai_charge, 0, soldier_attack1_refire1 },
	{ ai_charge, 0, soldierh_hyper_refire1 },
	{ ai_charge, 0, soldier_cock },
	{ ai_charge, 0, soldier_attack1_refire2 },
	{ ai_charge, 0, soldierh_hyper_laser_sound_end },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t soldierh_move_attack1 = { FRAME_attak101, FRAME_attak112, soldierh_frames_attack1, soldier_run };

// ATTACK2 (blaster/shotgun)

static void soldier_fire2 (edict_t *self)
{
	soldier_fire (self, 1, false);
}

static void soldier_attack2_refire1 (edict_t *self)
{
	if (self->count <= 0)
		self->monsterinfo.nextframe = FRAME_attak216;

	if (!self->enemy || self->count > 1 || self->enemy->health <= 0)
		return;

	if ((random() < 0.5 && visible (self, self->enemy)) || range_to (self, self->enemy) <= RANGE_MELEE_RR)
		self->monsterinfo.nextframe = FRAME_attak204;
}

static void soldier_attack2_refire2 (edict_t *self)
{
	if (!self->enemy || self->count < 2 || self->enemy->health <= 0)
		return;

	if (((self->radius_dmg || random() < 0.5) && visible (self, self->enemy))
		|| ((self->style == 0 || self->count < 4) && range_to (self, self->enemy) <= RANGE_MELEE_RR))
	{
		self->monsterinfo.nextframe = FRAME_attak204;
		self->radius_dmg = 0;
	}
}

static void soldier_attack2_shotgun_check (edict_t *self)
{
	if (self->dmg)
	{
		self->monsterinfo.nextframe = FRAME_attak210;
		// force a refire
		self->radius_dmg = 1;
	}
}

static mframe_t soldier_frames_attack2[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, soldier_attack2_shotgun_check },
	{ ai_charge },
	{ ai_charge, 0, soldier_fire2 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, soldier_attack2_refire1 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, soldier_cock },
	{ ai_charge },
	{ ai_charge, 0, soldier_attack2_refire2 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t soldier_move_attack2 = { FRAME_attak201, FRAME_attak218, soldier_frames_attack2, soldier_run };

static void soldierh_hyper_refire2 (edict_t *self)
{
	if (!self->enemy || self->count < 2)
		return;
	if (self->count < 4 && random() < 0.7 && visible (self, self->enemy))
		self->s.frame = FRAME_attak205;
}

static void soldierh_hyperripper2 (edict_t *self)
{
	if (self->count < 4)
		soldier_fire (self, 1, false);
}

static mframe_t soldierh_frames_attack2[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, soldierh_hyper_laser_sound_start },
	{ ai_charge, 0, soldier_fire2 },
	{ ai_charge, 0, soldierh_hyperripper2 },
	{ ai_charge, 0, soldierh_hyperripper2 },
	{ ai_charge, 0, soldier_attack2_refire1 },
	{ ai_charge, 0, soldierh_hyper_refire2 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, soldier_cock },
	{ ai_charge },
	{ ai_charge, 0, soldier_attack2_refire2 },
	{ ai_charge, 0, soldierh_hyper_laser_sound_end },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t soldierh_move_attack2 = { FRAME_attak201, FRAME_attak218, soldierh_frames_attack2, soldier_run };

// ATTACK3 (duck and shoot)

static void soldier_fire3 (edict_t *self)
{
	soldier_fire (self, 2, false);
}

static void soldierh_hyperripper3 (edict_t *self)
{
	if (self->s.skinnum >= 6 && self->count < 4)
		soldier_fire (self, 2, false);
}

static void soldier_attack3_refire (edict_t *self)
{
	if (self->dmg)
		monster_duck_hold (self);
	else if (level.time + 0.4 < self->monsterinfo.duck_wait_time)
		self->monsterinfo.nextframe = FRAME_attak303;
}

static mframe_t soldier_frames_attack3[] = {
	{ ai_charge, 0, monster_duck_down },
	{ ai_charge, 0, soldierh_hyper_laser_sound_start },
	{ ai_charge, 0, soldier_fire3 },
	{ ai_charge, 0, soldierh_hyperripper3 },
	{ ai_charge, 0, soldierh_hyperripper3 },
	{ ai_charge, 0, soldier_attack3_refire },
	{ ai_charge, 0, monster_duck_up },
	{ ai_charge, 0, soldierh_hyper_laser_sound_end },
	{ ai_charge }
};
static mmove_t soldier_move_attack3 = { FRAME_attak301, FRAME_attak309, soldier_frames_attack3, soldier_run };

// ATTACK4 (machinegun)

static void soldier_fire4 (edict_t *self)
{
	soldier_fire (self, 3, false);
}

static mframe_t soldier_frames_attack4[] = {
	{ ai_charge },
	{ ai_charge, 0, soldierh_hyper_laser_sound_start },
	{ ai_charge, 0, soldier_fire4 },
	{ ai_charge, 0, soldierh_hyper_laser_sound_end },
	{ ai_charge },
	{ ai_charge }
};
static mmove_t soldier_move_attack4 = { FRAME_attak401, FRAME_attak406, soldier_frames_attack4, soldier_run };

// ATTACK6 (run & shoot)

static void soldier_fire8 (edict_t *self)
{
	soldier_fire (self, 7, true);
}

static void soldier_attack6_refire1 (edict_t *self)
{
	// make sure dodge & charge bits are cleared
	monster_done_dodge (self);
	soldier_stop_charge (self);

	if (!self->enemy || self->count > 1)
		return;

	if (self->enemy->health <= 0 || range_to (self, self->enemy) < RANGE_NEAR_RR
		|| !visible (self, self->enemy))	// don't endlessly run into walls
	{
		soldier_run (self);
		return;
	}

	if (random() < 0.25)
		self->monsterinfo.nextframe = FRAME_runs03;
	else
		soldier_run (self);
}

static void soldier_attack6_refire2 (edict_t *self)
{
	// make sure dodge & charge bits are cleared
	monster_done_dodge (self);
	soldier_stop_charge (self);

	if (!self->enemy || self->count <= 0)
		return;

	if (self->enemy->health <= 0 || (!self->radius_dmg && range_to (self, self->enemy) < RANGE_NEAR_RR)
		|| !visible (self, self->enemy))	// don't endlessly run into walls
	{
		soldierh_hyper_laser_sound_end (self);
		return;
	}

	if (self->radius_dmg || random() < 0.25)
	{
		self->monsterinfo.nextframe = FRAME_runs03;
		self->radius_dmg = 0;
	}
}

static void soldier_attack6_shotgun_check (edict_t *self)
{
	if (self->dmg)
	{
		self->monsterinfo.nextframe = FRAME_runs09;
		// force a refire
		self->radius_dmg = 1;
	}
}

static void soldierh_hyperripper8 (edict_t *self)
{
	if (self->s.skinnum >= 6 && self->count < 4)
		soldier_fire (self, 7, true);
}

static void soldier_fire8_step (edict_t *self)
{
	soldier_fire8 (self);
	monster_footstep (self);
}

static void soldierh_hyperripper8_dodge (edict_t *self)
{
	soldierh_hyperripper8 (self);
	monster_done_dodge (self);
}

static mframe_t soldier_frames_attack6[] = {
	{ ai_run, 10, soldier_start_charge },
	{ ai_run, 4, soldier_attack6_shotgun_check },
	{ ai_run, 12, soldierh_hyper_laser_sound_start },
	{ ai_run, 11, soldier_fire8_step },
	{ ai_run, 13, soldierh_hyperripper8_dodge },
	{ ai_run, 18, soldierh_hyperripper8 },
	{ ai_run, 15, monster_footstep },
	{ ai_run, 14, soldier_attack6_refire1 },
	{ ai_run, 11 },
	{ ai_run, 8, monster_footstep },
	{ ai_run, 11, soldier_cock },
	{ ai_run, 12 },
	{ ai_run, 12, monster_footstep },
	{ ai_run, 17, soldier_attack6_refire2 }
};
static mmove_t soldier_move_attack6 = { FRAME_runs01, FRAME_runs14, soldier_frames_attack6, soldier_run };

static void soldier_attack (edict_t *self)
{
	float		r, chance;
	vec3_t		clear;
	qboolean	attack1_possible, attack2_possible;

	monster_done_dodge (self);

	// blindfire
	if (self->monsterinfo.attack_state == AS_BLIND)
	{
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

		if (self->style == 1)
			self->monsterinfo.currentmove = &soldierh_move_attack1;
		else
			self->monsterinfo.currentmove = &soldier_move_attack1;
		self->monsterinfo.attack_finished = level.time + 1.5 + random();
		return;
	}

	// run toward you and shoot instead of just stopping and shooting
	r = random();

	if (!(self->monsterinfo.aiflags & (AI_BLOCKED | AI_STAND_GROUND)) && r < 0.25
		&& self->count <= 3 && range_to (self, self->enemy) >= RANGE_NEAR_RR * 0.5)
	{
		self->monsterinfo.currentmove = &soldier_move_attack6;
		return;
	}

	if (self->count < 4)
	{
		// the shotgun guard only uses attack2 at close range
		if (!self->style && self->count >= 2 && self->count <= 3
			&& range_to (self, self->enemy) <= RANGE_NEAR_RR * 0.65)
			attack1_possible = false;
		else
			attack1_possible = M_CheckClearShot (self, monster_flash_offset[MZ2_SOLDIER_BLASTER_1], clear);

		attack2_possible = M_CheckClearShot (self, monster_flash_offset[MZ2_SOLDIER_BLASTER_2], clear);

		if (attack1_possible && (!attack2_possible || random() < 0.5))
		{
			if (self->style == 1)
				self->monsterinfo.currentmove = &soldierh_move_attack1;
			else
				self->monsterinfo.currentmove = &soldier_move_attack1;
		}
		else if (attack2_possible)
		{
			if (self->style == 1)
				self->monsterinfo.currentmove = &soldierh_move_attack2;
			else
				self->monsterinfo.currentmove = &soldier_move_attack2;
		}
	}
	else if (M_CheckClearShot (self, monster_flash_offset[MZ2_SOLDIER_MACHINEGUN_4], clear))
		self->monsterinfo.currentmove = &soldier_move_attack4;
}

// SIGHT

static void soldier_sight (edict_t *self, edict_t *other)
{
	vec3_t	clear;

	if (random() < 0.5)
		gi.sound (self, CHAN_VOICE, sound_sight1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_sight2, 1, ATTN_NORM, 0);

	// run and shoot, if he can see them
	if (self->enemy && range_to (self, self->enemy) >= RANGE_NEAR_RR && visible (self, self->enemy)
		&& (self->style == 1 || random() > 0.75))
	{
		// not the machinegun or laser, whose animation is a bit weird
		if (self->count < 4)
			self->monsterinfo.currentmove = &soldier_move_attack6;
		else if (M_CheckClearShot (self, monster_flash_offset[MZ2_SOLDIER_MACHINEGUN_4], clear))
			self->monsterinfo.currentmove = &soldier_move_attack4;
	}
}

// DUCK

static mframe_t soldier_frames_duck[] = {
	{ ai_move, 5, monster_duck_down },
	{ ai_move, -1, monster_duck_hold },
	{ ai_move, 1 },
	{ ai_move, 0, monster_duck_up },
	{ ai_move, 5 }
};
static mmove_t soldier_move_duck = { FRAME_duck01, FRAME_duck05, soldier_frames_duck, soldier_run };

static void soldier_stand_up (edict_t *self)
{
	soldierh_hyper_laser_sound_end (self);
	self->monsterinfo.currentmove = &soldier_move_trip;
	self->monsterinfo.nextframe = FRAME_runt08;
}

static qboolean soldier_prone_shoot_ok (edict_t *self)
{
	vec3_t	fwd, diff;

	if (!self->enemy || !self->enemy->inuse)
		return false;

	AngleVectors (self->s.angles, fwd, NULL, NULL);
	VectorSubtract (self->enemy->s.origin, self->s.origin, diff);
	diff[2] = 0;
	VectorNormalize (diff);
	return DotProduct (fwd, diff) >= 0.80;
}

static void ai_soldier_move (edict_t *self, float dist)
{
	ai_move (self, dist);

	if (!soldier_prone_shoot_ok (self))
		soldier_stand_up (self);
}

static void soldier_fire5 (edict_t *self)
{
	soldier_fire (self, 8, true);
}

static void soldierh_hyperripper5 (edict_t *self)
{
	if (self->style && self->count < 4)
		soldier_fire (self, 8, true);
}

static mframe_t soldier_frames_attack5[] = {
	{ ai_move, 18, monster_duck_down },
	{ ai_move, 11, monster_footstep },
	{ ai_move, 0, monster_footstep },
	{ ai_soldier_move },
	{ ai_soldier_move, 0, soldierh_hyper_laser_sound_start },
	{ ai_soldier_move, 0, soldier_fire5 },
	{ ai_soldier_move, 0, soldierh_hyperripper5 },
	{ ai_soldier_move, 0, soldierh_hyperripper5 },
};
static mmove_t soldier_move_attack5 = { FRAME_attak501, FRAME_attak508, soldier_frames_attack5, soldier_stand_up };

static void monster_check_prone (edict_t *self)
{
	// a shotgun guard waiting to cock
	if (!self->style && self->count >= 2 && self->count <= 3 && self->dmg)
		return;

	// not going to shoot at this angle
	if (!soldier_prone_shoot_ok (self))
		return;

	self->monsterinfo.currentmove = &soldier_move_attack5;
}

static mframe_t soldier_frames_trip[] = {
	{ ai_move, 10 },
	{ ai_move, 2, monster_check_prone },
	{ ai_move, 18, monster_duck_down },
	{ ai_move, 11, monster_footstep },
	{ ai_move, 9 },
	{ ai_move, -11, monster_footstep },
	{ ai_move, -2 },
	{ ai_move, 0 },
	{ ai_move, 6 },
	{ ai_move, -5 },
	{ ai_move, 0 },
	{ ai_move, 1 },
	{ ai_move, 0, monster_footstep },
	{ ai_move, 0, monster_duck_up },
	{ ai_move, 3 },
	{ ai_move, 2, monster_footstep },
	{ ai_move, -1 },
	{ ai_move, 2 },
	{ ai_move, 0 },
};
static mmove_t soldier_move_trip = { FRAME_runt01, FRAME_runt19, soldier_frames_trip, soldier_run };

static qboolean soldier_blocked (edict_t *self, float dist)
{
	// don't do anything if you're dodging
	if (self->monsterinfo.aiflags & (AI_DODGING | AI_DUCKED))
		return false;

	return blocked_checkplat (self, dist);
}

// DEATH

static void soldier_fire6 (edict_t *self)
{
	soldier_fire (self, 5, false);
	if (self->dmg)
		self->monsterinfo.nextframe = FRAME_death126;
}

static void soldier_fire7 (edict_t *self)
{
	soldier_fire (self, 6, false);
}

static void soldier_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

static void soldier_death_shrink (edict_t *self)
{
	self->svflags |= SVF_DEADMONSTER;
	self->maxs[2] = 0;
	gi.linkentity (self);
}

static mframe_t soldier_frames_death1[] = {
	{ ai_move },
	{ ai_move, -10 },
	{ ai_move, -10 },
	{ ai_move, -10, soldier_death_shrink },
	{ ai_move, -5 },
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

	{ ai_move, 0, soldierh_hyper_laser_sound_start },
	{ ai_move, 0, soldier_fire6 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, soldier_fire7 },
	{ ai_move, 0, soldierh_hyper_laser_sound_end },
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
static mmove_t soldier_move_death1 = { FRAME_death101, FRAME_death136, soldier_frames_death1, soldier_dead };

static mframe_t soldier_frames_death2[] = {
	{ ai_move, -5 },
	{ ai_move, -5 },
	{ ai_move, -5 },
	{ ai_move, 0, soldier_death_shrink },
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
static mmove_t soldier_move_death2 = { FRAME_death201, FRAME_death235, soldier_frames_death2, soldier_dead };

static mframe_t soldier_frames_death3[] = {
	{ ai_move, -5 },
	{ ai_move, -5 },
	{ ai_move, -5 },
	{ ai_move, 0, soldier_death_shrink },
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
};
static mmove_t soldier_move_death3 = { FRAME_death301, FRAME_death345, soldier_frames_death3, soldier_dead };

static mframe_t soldier_frames_death4[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move, 1.5f },
	{ ai_move, 2.5f },
	{ ai_move, -1.5f },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -0.5f },
	{ ai_move },

	{ ai_move },
	{ ai_move, 4.0f },
	{ ai_move, 4.0f },
	{ ai_move, 8.0f, soldier_death_shrink },
	{ ai_move, 8.0f },
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
	{ ai_move, 5.5f },

	{ ai_move, 2.5f },
	{ ai_move, -2.0f },
	{ ai_move, -2.0f }
};
static mmove_t soldier_move_death4 = { FRAME_death401, FRAME_death453, soldier_frames_death4, soldier_dead };

static mframe_t soldier_frames_death5[] = {
	{ ai_move, -5 },
	{ ai_move, -5 },
	{ ai_move, -5 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, soldier_death_shrink },
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
static mmove_t soldier_move_death5 = { FRAME_death501, FRAME_death524, soldier_frames_death5, soldier_dead };

static mframe_t soldier_frames_death6[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, soldier_death_shrink },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t soldier_move_death6 = { FRAME_death601, FRAME_death610, soldier_frames_death6, soldier_dead };

static gib_def_t soldier_gibs[] = {
	{ 3, "models/objects/gibs/sm_meat/tris.md2" },
	{ 1, "models/objects/gibs/bone2/tris.md2" },
	{ 1, "models/objects/gibs/bone/tris.md2" },
	{ 1, "models/monsters/soldier/gibs/arm.md2", GIB_SKINNED },
	{ 1, "models/monsters/soldier/gibs/gun.md2", GIB_SKINNED },
	{ 1, "models/monsters/soldier/gibs/chest.md2", GIB_SKINNED },
	{ 1, "models/monsters/soldier/gibs/head.md2", GIB_HEAD | GIB_SKINNED },
	{ 0, NULL }
};

static void soldier_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	int		n;

	soldierh_hyper_laser_sound_end (self);

	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		self->s.skinnum /= 2;
		ThrowGibs (self, damage, soldier_gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	n = self->count | 1;
	if (n == 1)
		gi.sound (self, CHAN_VOICE, sound_death_light, 1, ATTN_NORM, 0);
	else if (n == 3)
		gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_death_ss, 1, ATTN_NORM, 0);

	if (fabs ((self->s.origin[2] + self->viewheight) - point[2]) <= 4 && self->velocity[2] < 65)
	{
		// head shot
		self->monsterinfo.currentmove = &soldier_move_death3;
		return;
	}

	// dying on the ground, a quicker death4
	if (self->monsterinfo.currentmove == &soldier_move_trip
		|| self->monsterinfo.currentmove == &soldier_move_attack5)
	{
		self->monsterinfo.currentmove = &soldier_move_death4;
		self->monsterinfo.nextframe = FRAME_death413;
		soldier_death_shrink (self);
		return;
	}

	// only the spin-death with velocity enough to justify it
	if (self->velocity[2] > 65 || VectorLength (self->velocity) > 150)
		n = rand() % 5;
	else
		n = rand() % 4;

	if (n == 0)
		self->monsterinfo.currentmove = &soldier_move_death1;
	else if (n == 1)
		self->monsterinfo.currentmove = &soldier_move_death2;
	else if (n == 2)
		self->monsterinfo.currentmove = &soldier_move_death4;
	else if (n == 3)
		self->monsterinfo.currentmove = &soldier_move_death5;
	else
		self->monsterinfo.currentmove = &soldier_move_death6;
}

// DODGE

static qboolean soldier_sidestep (edict_t *self)
{
	// don't sidestep during trip or up pain
	if (self->monsterinfo.currentmove == &soldier_move_trip
		|| self->monsterinfo.currentmove == &soldier_move_attack5
		|| self->monsterinfo.currentmove == &soldier_move_pain4)
		return false;

	if (self->count <= 3)
	{
		if (self->monsterinfo.currentmove != &soldier_move_attack6)
		{
			self->monsterinfo.currentmove = &soldier_move_attack6;
			soldierh_hyper_laser_sound_end (self);
		}
	}
	else if (self->monsterinfo.currentmove != &soldier_move_start_run
		&& self->monsterinfo.currentmove != &soldier_move_run)
	{
		self->monsterinfo.currentmove = &soldier_move_start_run;
		soldierh_hyper_laser_sound_end (self);
	}

	return true;
}

static qboolean soldier_duck (edict_t *self, float eta)
{
	self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

	if (self->monsterinfo.currentmove == &soldier_move_attack6)
		self->monsterinfo.currentmove = &soldier_move_trip;
	else if (self->dmg || (rand() & 1))
		self->monsterinfo.currentmove = &soldier_move_duck;
	else
		self->monsterinfo.currentmove = &soldier_move_attack3;

	soldierh_hyper_laser_sound_end (self);
	return true;
}

// BLIND

static mframe_t soldier_frames_blind[] = {
	{ ai_move, 0, soldier_idle },
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
static mmove_t soldier_move_blind = { FRAME_stand101, FRAME_stand130, soldier_frames_blind, soldier_blind };

static void soldier_blind (edict_t *self)
{
	self->monsterinfo.currentmove = &soldier_move_blind;
}

// SPAWN

#define	SPAWNFLAG_SOLDIER_BLIND		8

static void SP_monster_soldier_x_rr (edict_t *self)
{
	self->s.modelindex = gi.modelindex ("models/monsters/soldier/tris.md2");
	self->monsterinfo.scale = MODEL_SCALE;
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, 32);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	sound_idle = gi.soundindex ("soldier/solidle1.wav");
	sound_sight1 = gi.soundindex ("soldier/solsght1.wav");
	sound_sight2 = gi.soundindex ("soldier/solsrch1.wav");
	sound_cock = gi.soundindex ("infantry/infatck3.wav");

	gi.modelindex ("models/monsters/soldier/gibs/head.md2");
	gi.modelindex ("models/monsters/soldier/gibs/gun.md2");
	gi.modelindex ("models/monsters/soldier/gibs/arm.md2");
	gi.modelindex ("models/monsters/soldier/gibs/chest.md2");

	self->mass = 100;
	self->gib_health = -30;

	self->pain = soldier_pain;
	self->die = soldier_die;

	self->monsterinfo.stand = soldier_stand;
	self->monsterinfo.walk = soldier_walk;
	self->monsterinfo.run = soldier_run;
	self->monsterinfo.dodge = (void (*)(edict_t *, edict_t *, float))M_MonsterDodge;
	self->monsterinfo.attack = soldier_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = soldier_sight;
	self->monsterinfo.setskin = soldier_setskin;

	self->monsterinfo.blocked = soldier_blocked;
	self->monsterinfo.duck = soldier_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = soldier_sidestep;

	if (self->spawnflags & SPAWNFLAG_SOLDIER_BLIND)
		self->monsterinfo.stand = soldier_blind;

	gi.linkentity (self);

	self->monsterinfo.stand (self);

	walkmonster_start (self);
}

void SP_monster_soldier_light_rr (edict_t *self)
{
	SP_monster_soldier_x_rr (self);

	sound_pain_light = gi.soundindex ("soldier/solpain2.wav");
	sound_death_light = gi.soundindex ("soldier/soldeth2.wav");
	gi.modelindex ("models/objects/laser/tris.md2");
	gi.soundindex ("misc/lasfly.wav");
	gi.soundindex ("soldier/solatck2.wav");

	self->s.skinnum = 0;
	self->count = self->s.skinnum;
	self->health = self->max_health = 20;
	self->monsterinfo.blindfire = true;
}

void SP_monster_soldier_rr (edict_t *self)
{
	SP_monster_soldier_x_rr (self);

	sound_pain = gi.soundindex ("soldier/solpain1.wav");
	sound_death = gi.soundindex ("soldier/soldeth1.wav");
	gi.soundindex ("soldier/solatck1.wav");

	self->s.skinnum = 2;
	self->count = self->s.skinnum;
	self->health = self->max_health = 30;
}

void SP_monster_soldier_ss_rr (edict_t *self)
{
	SP_monster_soldier_x_rr (self);

	sound_pain_ss = gi.soundindex ("soldier/solpain3.wav");
	sound_death_ss = gi.soundindex ("soldier/soldeth3.wav");
	gi.soundindex ("soldier/solatck3.wav");

	self->s.skinnum = 4;
	self->count = self->s.skinnum;
	self->health = self->max_health = 40;
}

static void SP_monster_soldier_h_rr (edict_t *self)
{
	SP_monster_soldier_x_rr (self);
	self->style = 1;
}

void SP_monster_soldier_ripper_rr (edict_t *self)
{
	SP_monster_soldier_h_rr (self);

	sound_pain_light = gi.soundindex ("soldier/solpain2.wav");
	sound_death_light = gi.soundindex ("soldier/soldeth2.wav");
	gi.modelindex ("models/objects/boomrang/tris.md2");
	gi.soundindex ("misc/lasfly.wav");
	gi.soundindex ("soldier/solatck2.wav");

	self->s.skinnum = 6;
	self->count = self->s.skinnum - 6;
	self->health = self->max_health = 50;
	self->monsterinfo.blindfire = true;
}

void SP_monster_soldier_hypergun_rr (edict_t *self)
{
	SP_monster_soldier_h_rr (self);

	gi.modelindex ("models/objects/laser/tris.md2");
	sound_pain = gi.soundindex ("soldier/solpain1.wav");
	sound_death = gi.soundindex ("soldier/soldeth1.wav");
	gi.soundindex ("soldier/solatck1.wav");
	gi.soundindex ("weapons/hyprbd1a.wav");
	gi.soundindex ("weapons/hyprbl1a.wav");

	self->s.skinnum = 8;
	self->count = self->s.skinnum - 6;
	self->health = self->max_health = 60;
	self->monsterinfo.blindfire = true;
}

void SP_monster_soldier_lasergun_rr (edict_t *self)
{
	SP_monster_soldier_h_rr (self);

	sound_pain_ss = gi.soundindex ("soldier/solpain3.wav");
	sound_death_ss = gi.soundindex ("soldier/soldeth3.wav");
	gi.soundindex ("soldier/solatck3.wav");

	self->s.skinnum = 10;
	self->count = self->s.skinnum - 6;
	self->health = self->max_health = 70;
}
