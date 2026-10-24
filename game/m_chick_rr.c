// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

IRON MAIDEN, the 2023 re-release's

Its m_chick.cpp, in C, for its own maps (RR_Map): an iron maiden whose
rockets now and then lead you, who fires again only with a clear shot,
blind-fires where she last saw you, slashes and keeps slashing while
you stay close, ducks and sidesteps, and gibs into pieces of her own;
and The Reckoning's, whose rockets seek heat. Her frames are 1997's.
m_chick.c is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10. Her heat seekers turn
as The Reckoning's do.
==============================================================================
*/

#include "g_local.h"
#include "m_chick.h"

qboolean blocked_checkplat (edict_t *self, float dist);
qboolean visible (edict_t *self, edict_t *other);

static int	sound_missile_prelaunch;
static int	sound_missile_launch;
static int	sound_melee_swing;
static int	sound_melee_hit;
static int	sound_missile_reload;
static int	sound_death1;
static int	sound_death2;
static int	sound_fall_down;
static int	sound_idle1;
static int	sound_idle2;
static int	sound_pain1;
static int	sound_pain2;
static int	sound_pain3;
static int	sound_sight;
static int	sound_search;

static void chick_stand (edict_t *self);
static void chick_run (edict_t *self);
static void chick_reslash (edict_t *self);
static void chick_rerocket (edict_t *self);
static void chick_attack1 (edict_t *self);
static void chick_slash (edict_t *self);
static void chick_dead (edict_t *self);
static void chick_shrink (edict_t *self);

static void ChickMoan (edict_t *self)
{
	if (random() < 0.5f)
		gi.sound (self, CHAN_VOICE, sound_idle1, 1, ATTN_IDLE, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_idle2, 1, ATTN_IDLE, 0);
}

static void chick_rerocket_step (edict_t *self)
{
	chick_rerocket (self);
	monster_footstep (self);
}

static mframe_t chick_frames_fidget[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, ChickMoan },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
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
static mmove_t chick_move_fidget = { FRAME_stand201, FRAME_stand230, chick_frames_fidget, chick_stand };

static void chick_fidget (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		return;
	else if (self->enemy)
		return;
	if (random() <= 0.3f)
		self->monsterinfo.currentmove = &chick_move_fidget;
}

static mframe_t chick_frames_stand[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, chick_fidget },
};
static mmove_t chick_move_stand = { FRAME_stand101, FRAME_stand130, chick_frames_stand, NULL };

static void chick_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &chick_move_stand;
}

static mframe_t chick_frames_start_run[] = {
	{ ai_run, 1 },
	{ ai_run },
	{ ai_run, 0, monster_footstep },
	{ ai_run, -1 },
	{ ai_run, -1, monster_footstep },
	{ ai_run },
	{ ai_run, 1 },
	{ ai_run, 3 },
	{ ai_run, 6 },
	{ ai_run, 3 }
};
static mmove_t chick_move_start_run = { FRAME_walk01, FRAME_walk10, chick_frames_start_run, chick_run };

static mframe_t chick_frames_run[] = {
	{ ai_run, 6 },
	{ ai_run, 8, monster_footstep },
	{ ai_run, 13 },
	{ ai_run, 5, monster_done_dodge }, // make sure to clear dodge bit
	{ ai_run, 7 },
	{ ai_run, 4 },
	{ ai_run, 11, monster_footstep },
	{ ai_run, 5 },
	{ ai_run, 9 },
	{ ai_run, 7 }
};
static mmove_t chick_move_run = { FRAME_walk11, FRAME_walk20, chick_frames_run, NULL };

static mframe_t chick_frames_walk[] = {
	{ ai_walk, 6 },
	{ ai_walk, 8, monster_footstep },
	{ ai_walk, 13 },
	{ ai_walk, 5 },
	{ ai_walk, 7 },
	{ ai_walk, 4 },
	{ ai_walk, 11, monster_footstep },
	{ ai_walk, 5 },
	{ ai_walk, 9 },
	{ ai_walk, 7 }
};
static mmove_t chick_move_walk = { FRAME_walk11, FRAME_walk20, chick_frames_walk, NULL };

static void chick_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &chick_move_walk;
}

static void chick_run (edict_t *self)
{
	monster_done_dodge (self);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.currentmove = &chick_move_stand;
		return;
	}

	if (self->monsterinfo.currentmove == &chick_move_walk ||
		self->monsterinfo.currentmove == &chick_move_start_run)
	{
		self->monsterinfo.currentmove = &chick_move_run;
	}
	else
	{
		self->monsterinfo.currentmove = &chick_move_start_run;
	}
}

static mframe_t chick_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t chick_move_pain1 = { FRAME_pain101, FRAME_pain105, chick_frames_pain1, chick_run };

static mframe_t chick_frames_pain2[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t chick_move_pain2 = { FRAME_pain201, FRAME_pain205, chick_frames_pain2, chick_run };

static mframe_t chick_frames_pain3[] = {
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move, -6 },
	{ ai_move, 3, monster_footstep },
	{ ai_move, 11 },
	{ ai_move, 3, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move, 4 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move, -3 },
	{ ai_move, -4 },
	{ ai_move, 5 },
	{ ai_move, 7 },
	{ ai_move, -2 },
	{ ai_move, 3 },
	{ ai_move, -5 },
	{ ai_move, -2 },
	{ ai_move, -8 },
	{ ai_move, 2, monster_footstep }
};
static mmove_t chick_move_pain3 = { FRAME_pain301, FRAME_pain321, chick_frames_pain3, chick_run };

static void chick_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	float	r;

	monster_done_dodge (self);

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	r = random();
	if (r < 0.33f)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else if (r < 0.66f)
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain3, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	// PMM - clear this from blindfire
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

	if (damage <= 10)
		self->monsterinfo.currentmove = &chick_move_pain1;
	else if (damage <= 25)
		self->monsterinfo.currentmove = &chick_move_pain2;
	else
		self->monsterinfo.currentmove = &chick_move_pain3;

	// PMM - clear duck flag
	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up (self);
}

static void chick_setpain (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

static void chick_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, 0);
	VectorSet (self->maxs, 16, 16, 8);
	monster_dead (self);
}

static void chick_shrink (edict_t *self)
{
	self->maxs[2] = 12;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static mframe_t chick_frames_death2[] = {
	{ ai_move, -6 },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -5, monster_footstep },
	{ ai_move },
	{ ai_move, -1 },
	{ ai_move, -2 },
	{ ai_move, 1 },
	{ ai_move, 10 },
	{ ai_move, 2 },
	{ ai_move, 3, monster_footstep },
	{ ai_move, 1 },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move, 3 },
	{ ai_move, 1, monster_footstep },
	{ ai_move, -3 },
	{ ai_move, -5 },
	{ ai_move, 4 },
	{ ai_move, 15, chick_shrink },
	{ ai_move, 14, monster_footstep },
	{ ai_move, 1 }
};
static mmove_t chick_move_death2 = { FRAME_death201, FRAME_death223, chick_frames_death2, chick_dead };

static mframe_t chick_frames_death1[] = {
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move, -7 },
	{ ai_move, 4, monster_footstep },
	{ ai_move, 11, chick_shrink },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move }
};
static mmove_t chick_move_death1 = { FRAME_death101, FRAME_death112, chick_frames_death1, chick_dead };

static gib_def_t chick_gibs[] = {
	{ 2, "models/objects/gibs/bone/tris.md2" },
	{ 3, "models/objects/gibs/sm_meat/tris.md2" },
	{ 1, "models/monsters/bitch/gibs/arm.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/bitch/gibs/foot.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/bitch/gibs/tube.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/bitch/gibs/chest.md2", GIB_SKINNED },
	{ 1, "models/monsters/bitch/gibs/head.md2", GIB_HEAD | GIB_SKINNED },
	{ 0, NULL }
};

static void chick_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs (self, damage, chick_gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	if (!(rand () & 1))
	{
		self->monsterinfo.currentmove = &chick_move_death1;
		gi.sound (self, CHAN_VOICE, sound_death1, 1, ATTN_NORM, 0);
	}
	else
	{
		self->monsterinfo.currentmove = &chick_move_death2;
		gi.sound (self, CHAN_VOICE, sound_death2, 1, ATTN_NORM, 0);
	}
}

static mframe_t chick_frames_duck[] = {
	{ ai_move, 0, monster_duck_down },
	{ ai_move, 1 },
	{ ai_move, 4, monster_duck_hold },
	{ ai_move, -4 },
	{ ai_move, -5, monster_duck_up },
	{ ai_move, 3 },
	{ ai_move, 1 }
};
static mmove_t chick_move_duck = { FRAME_duck01, FRAME_duck07, chick_frames_duck, chick_run };

static void ChickSlash (edict_t *self)
{
	vec3_t	aim;

	VectorSet (aim, MELEE_DISTANCE_RR, self->mins[0], 10);
	gi.sound (self, CHAN_WEAPON, sound_melee_swing, 1, ATTN_NORM, 0);
	fire_hit_rr (self, aim, 10 + rand () % 6, 100);
}

// a rocket, or her heat seeker
static void chick_fire (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	// RAFAEL
	if (self->s.skinnum > 1)
		monster_fire_heat (self, start, dir, 50, speed, MZ2_CHICK_ROCKET_1);
	else
		// RAFAEL
		monster_fire_rocket (self, start, dir, 50, speed, MZ2_CHICK_ROCKET_1);
}

static void ChickRocket (edict_t *self)
{
	vec3_t		forward, right;
	vec3_t		start;
	vec3_t		dir;
	vec3_t		vec;
	trace_t		trace; // PMM - check target
	int			rocketSpeed;
	// pmm - blindfire
	vec3_t		target;
	qboolean	blindfire;

	blindfire = (self->monsterinfo.aiflags & AI_MANUAL_STEERING) != 0;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_CHICK_ROCKET_1], forward, right, start);

	// [Paril-KEX]
	if (self->s.skinnum > 1)
		rocketSpeed = 500;
	else
		rocketSpeed = 650;

	// PMM
	if (blindfire)
		VectorCopy (self->monsterinfo.blind_fire_target, target);
	else
		VectorCopy (self->enemy->s.origin, target);
	// pmm

	//  PMM - blindfire shooting
	if (blindfire)
	{
		VectorCopy (target, vec);
		VectorSubtract (vec, start, dir);
	}
	// pmm
	// don't shoot at feet if they're above where i'm shooting from.
	else if (random() < 0.33f || (start[2] < self->enemy->absmin[2]))
	{
		VectorCopy (target, vec);
		vec[2] += self->enemy->viewheight;
		VectorSubtract (vec, start, dir);
	}
	else
	{
		VectorCopy (target, vec);
		vec[2] = self->enemy->absmin[2] + 1;
		VectorSubtract (vec, start, dir);
	}

	//======
	// PMM - lead target  (not when blindfiring)
	// 20, 35, 50, 65 chance of leading
	if ((!blindfire) && (random() < 0.35f))
		PredictAim (self, self->enemy, start, rocketSpeed, false, 0.f, dir, vec);
	// PMM - lead target
	//======

	VectorNormalize (dir);

	// pmm blindfire doesn't check target (done in checkattack)
	// paranoia, make sure we're not shooting a target right next to us
	trace = gi.trace (start, NULL, NULL, vec, self, MASK_SHOT);
	if (blindfire)
	{
		// blindfire has different fail criteria for the trace
		if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
			chick_fire (self, start, dir, rocketSpeed);
		else
		{
			// geez, this is bad.  she's avoiding about 80% of her blindfires due to hitting things.
			// hunt around for a good shot
			// try shifting the target to the left a little (to help counter her large offset)
			VectorMA (target, -10, right, vec);
			VectorSubtract (vec, start, dir);
			VectorNormalize (dir);
			trace = gi.trace (start, NULL, NULL, vec, self, MASK_SHOT);
			if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
				chick_fire (self, start, dir, rocketSpeed);
			else
			{
				// ok, that failed.  try to the right
				VectorMA (target, 10, right, vec);
				VectorSubtract (vec, start, dir);
				VectorNormalize (dir);
				trace = gi.trace (start, NULL, NULL, vec, self, MASK_SHOT);
				if (!(trace.startsolid || trace.allsolid || (trace.fraction < 0.5f)))
					chick_fire (self, start, dir, rocketSpeed);
			}
		}
	}
	else
	{
		if (trace.fraction > 0.5f || trace.ent->solid != SOLID_BSP)
			chick_fire (self, start, dir, rocketSpeed);
	}
}

static void Chick_PreAttack1 (edict_t *self)
{
	vec3_t	aim;

	gi.sound (self, CHAN_VOICE, sound_missile_prelaunch, 1, ATTN_NORM, 0);

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		VectorSubtract (self->monsterinfo.blind_fire_target, self->s.origin, aim);
		self->ideal_yaw = vectoyaw (aim);
	}
}

static void ChickReload (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_missile_reload, 1, ATTN_NORM, 0);
}

static mframe_t chick_frames_start_attack1[] = {
	{ ai_charge, 0, Chick_PreAttack1 },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 4 },
	{ ai_charge },
	{ ai_charge, -3 },
	{ ai_charge, 3 },
	{ ai_charge, 5 },
	{ ai_charge, 7, monster_footstep },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, chick_attack1 }
};
static mmove_t chick_move_start_attack1 = { FRAME_attak101, FRAME_attak113, chick_frames_start_attack1, NULL };

static mframe_t chick_frames_attack1[] = {
	{ ai_charge, 19, ChickRocket },
	{ ai_charge, -6, monster_footstep },
	{ ai_charge, -5 },
	{ ai_charge, -2 },
	{ ai_charge, -7, monster_footstep },
	{ ai_charge },
	{ ai_charge, 1 },
	{ ai_charge, 10, ChickReload },
	{ ai_charge, 4 },
	{ ai_charge, 5, monster_footstep },
	{ ai_charge, 6 },
	{ ai_charge, 6 },
	{ ai_charge, 4 },
	{ ai_charge, 3, chick_rerocket_step }
};
static mmove_t chick_move_attack1 = { FRAME_attak114, FRAME_attak127, chick_frames_attack1, NULL };

static mframe_t chick_frames_end_attack1[] = {
	{ ai_charge, -3 },
	{ ai_charge },
	{ ai_charge, -6 },
	{ ai_charge, -4 },
	{ ai_charge, -2, monster_footstep }
};
static mmove_t chick_move_end_attack1 = { FRAME_attak128, FRAME_attak132, chick_frames_end_attack1, chick_run };

static void chick_rerocket (edict_t *self)
{
	vec3_t	clear;

	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		self->monsterinfo.currentmove = &chick_move_end_attack1;
		return;
	}

	if (!M_CheckClearShot (self, monster_flash_offset[MZ2_CHICK_ROCKET_1], clear))
	{
		self->monsterinfo.currentmove = &chick_move_end_attack1;
		return;
	}

	if (self->enemy->health > 0)
	{
		if (range_to (self, self->enemy) > RANGE_MELEE_RR)
			if (visible (self, self->enemy))
				if (random() <= 0.7f)
				{
					self->monsterinfo.currentmove = &chick_move_attack1;
					return;
				}
	}
	self->monsterinfo.currentmove = &chick_move_end_attack1;
}

static void chick_attack1 (edict_t *self)
{
	self->monsterinfo.currentmove = &chick_move_attack1;
}

static mframe_t chick_frames_slash[] = {
	{ ai_charge, 1 },
	{ ai_charge, 7, ChickSlash },
	{ ai_charge, -7, monster_footstep },
	{ ai_charge, 1 },
	{ ai_charge, -1 },
	{ ai_charge, 1 },
	{ ai_charge },
	{ ai_charge, 1 },
	{ ai_charge, -2, chick_reslash }
};
static mmove_t chick_move_slash = { FRAME_attak204, FRAME_attak212, chick_frames_slash, NULL };

static mframe_t chick_frames_end_slash[] = {
	{ ai_charge, -6 },
	{ ai_charge, -1 },
	{ ai_charge, -6 },
	{ ai_charge, 0, monster_footstep }
};
static mmove_t chick_move_end_slash = { FRAME_attak213, FRAME_attak216, chick_frames_end_slash, chick_run };

static void chick_reslash (edict_t *self)
{
	if (self->enemy->health > 0)
	{
		if (range_to (self, self->enemy) <= RANGE_MELEE_RR)
		{
			if (random() <= 0.9f)
			{
				self->monsterinfo.currentmove = &chick_move_slash;
				return;
			}
			else
			{
				self->monsterinfo.currentmove = &chick_move_end_slash;
				return;
			}
		}
	}
	self->monsterinfo.currentmove = &chick_move_end_slash;
}

static void chick_slash (edict_t *self)
{
	self->monsterinfo.currentmove = &chick_move_slash;
}

static mframe_t chick_frames_start_slash[] = {
	{ ai_charge, 1 },
	{ ai_charge, 8 },
	{ ai_charge, 3 }
};
static mmove_t chick_move_start_slash = { FRAME_attak201, FRAME_attak203, chick_frames_start_slash, chick_slash };

static void chick_melee (edict_t *self)
{
	self->monsterinfo.currentmove = &chick_move_start_slash;
}

static void chick_attack (edict_t *self)
{
	float	r, chance;
	vec3_t	clear;

	if (!M_CheckClearShot (self, monster_flash_offset[MZ2_CHICK_ROCKET_1], clear))
		return;

	monster_done_dodge (self);

	// PMM
	if (self->monsterinfo.attack_state == AS_BLIND)
	{
		// setup shot probabilities
		if (self->monsterinfo.blind_fire_delay < 1.0)
			chance = 1.0;
		else if (self->monsterinfo.blind_fire_delay < 7.5)
			chance = 0.4f;
		else
			chance = 0.1f;

		r = random();

		// minimum of 5.5 seconds, plus 0-1, after the shots are done
		self->monsterinfo.blind_fire_delay += 5.5 + random();

		// don't shoot at the origin
		if (VectorCompare (self->monsterinfo.blind_fire_target, vec3_origin))
			return;

		// don't shoot if the dice say not to
		if (r > chance)
			return;

		// turn on manual steering to signal both manual steering and blindfire
		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
		self->monsterinfo.currentmove = &chick_move_start_attack1;
		self->monsterinfo.attack_finished = level.time + 2 * random();
		return;
	}
	// pmm

	self->monsterinfo.currentmove = &chick_move_start_attack1;
}

static void chick_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static qboolean chick_blocked (edict_t *self, float dist)
{
	return blocked_checkplat (self, dist);
}

static qboolean chick_duck (edict_t *self, float eta)
{
	if ((self->monsterinfo.currentmove == &chick_move_start_attack1) ||
		(self->monsterinfo.currentmove == &chick_move_attack1))
	{
		// if we're shooting don't dodge
		self->monsterinfo.unduck (self);
		return false;
	}

	self->monsterinfo.currentmove = &chick_move_duck;

	return true;
}

static qboolean chick_sidestep (edict_t *self)
{
	if ((self->monsterinfo.currentmove == &chick_move_start_attack1) ||
		(self->monsterinfo.currentmove == &chick_move_attack1) ||
		(self->monsterinfo.currentmove == &chick_move_pain3))
	{
		// if we're shooting, don't dodge
		return false;
	}

	if (self->monsterinfo.currentmove != &chick_move_run)
		self->monsterinfo.currentmove = &chick_move_run;

	return true;
}

void SP_monster_chick_rr (edict_t *self)
{
	sound_missile_prelaunch = gi.soundindex ("chick/chkatck1.wav");
	sound_missile_launch = gi.soundindex ("chick/chkatck2.wav");
	sound_melee_swing = gi.soundindex ("chick/chkatck3.wav");
	sound_melee_hit = gi.soundindex ("chick/chkatck4.wav");
	sound_missile_reload = gi.soundindex ("chick/chkatck5.wav");
	sound_death1 = gi.soundindex ("chick/chkdeth1.wav");
	sound_death2 = gi.soundindex ("chick/chkdeth2.wav");
	sound_fall_down = gi.soundindex ("chick/chkfall1.wav");
	sound_idle1 = gi.soundindex ("chick/chkidle1.wav");
	sound_idle2 = gi.soundindex ("chick/chkidle2.wav");
	sound_pain1 = gi.soundindex ("chick/chkpain1.wav");
	sound_pain2 = gi.soundindex ("chick/chkpain2.wav");
	sound_pain3 = gi.soundindex ("chick/chkpain3.wav");
	sound_sight = gi.soundindex ("chick/chksght1.wav");
	sound_search = gi.soundindex ("chick/chksrch1.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/bitch/tris.md2");

	gi.modelindex ("models/monsters/bitch/gibs/arm.md2");
	gi.modelindex ("models/monsters/bitch/gibs/chest.md2");
	gi.modelindex ("models/monsters/bitch/gibs/foot.md2");
	gi.modelindex ("models/monsters/bitch/gibs/head.md2");
	gi.modelindex ("models/monsters/bitch/gibs/tube.md2");

	VectorSet (self->mins, -16, -16, 0);
	VectorSet (self->maxs, 16, 16, 56);

	self->health = 175;
	self->gib_health = -70;
	self->mass = 200;

	self->pain = chick_pain;
	self->die = chick_die;

	self->monsterinfo.stand = chick_stand;
	self->monsterinfo.walk = chick_walk;
	self->monsterinfo.run = chick_run;
	// pmm
	self->monsterinfo.dodge = (void (*)(edict_t *, edict_t *, float))M_MonsterDodge;
	self->monsterinfo.duck = chick_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = chick_sidestep;
	self->monsterinfo.blocked = chick_blocked; // PGM
	// pmm
	self->monsterinfo.attack = chick_attack;
	self->monsterinfo.melee = chick_melee;
	self->monsterinfo.sight = chick_sight;
	self->monsterinfo.setskin = chick_setpain;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &chick_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	// PMM
	self->monsterinfo.blindfire = true;
	// pmm
	walkmonster_start (self);

	// The Reckoning's, whose rockets seek heat
	if (!strcmp (self->classname, "monster_chick_heat"))
	{
		self->s.skinnum = 2;
		gi.soundindex ("weapons/railgr1a.wav");
	}
}
