/*
==============================================================================

Ground Zero's black widow (monster_widow), its final boss's first form,
from the re-release's rerelease/rogue/m_rogue_widow.cpp: it sweeps its
blaster as it turns, fires its rail, kicks, summons stalkers, answers
your powerups with its own, and leaves its legs to burn as it goes on
to its second form (m_widow2.c). This game has no other of it.

==============================================================================
*/

#include "g_local.h"
#include "m_widow.h"

#define RAIL_TIME			3
#define BLASTER_TIME		2
#define BLASTER2_DAMAGE		10
#define WIDOW_RAIL_DAMAGE	50

qboolean infront (edict_t *self, edict_t *other);
void Widowlegs_Spawn (vec3_t startpos, vec3_t angles);

static int	sound_pain1;
static int	sound_pain2;
static int	sound_pain3;
static int	sound_rail;

static unsigned	shotsfired;

static vec3_t spawnpoints[] = {
	{ 30, 100, 16 },
	{ 30, -100, 16 }
};

static vec3_t beameffects[] = {
	{ 12.58f, -43.71f, 68.88f },
	{ 3.43f, 58.72f, 68.41f }
};

static float sweep_angles[] = {
	32.f, 26.f, 20.f, 10.f, 0.f, -6.5f, -13.f, -27.f, -41.f
};

static vec3_t stalker_mins = { -28, -28, -18 };
static vec3_t stalker_maxs = { 28, 28, 18 };

unsigned	widow_damage_multiplier;	// its second form's too

static void widow_run (edict_t *self);
static void widow_attack_blaster (edict_t *self);
static void widow_reattack_blaster (edict_t *self);
static void widow_start_spawn (edict_t *self);
static void widow_done_spawn (edict_t *self);
static void widow_spawn_check (edict_t *self);
static void widow_ready_spawn (edict_t *self);
static void widow_attack_rail (edict_t *self);
static void widow_start_rail (edict_t *self);
static void widow_rail_done (edict_t *self);
static void widow_start_run_12 (edict_t *self);
static void widow_step (edict_t *self);
static void widow_stepshoot (edict_t *self);
static void widow_attack_kick (edict_t *self);
static void spawn_out_start (edict_t *self);
static void spawn_out_do (edict_t *self);
static void WidowBlaster (edict_t *self);
static void WidowRail (edict_t *self);
static void WidowSaveLoc (edict_t *self);
void WidowCalcSlots (edict_t *self);

static mframe_t widow_frames_stand[] = {
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

static mframe_t widow_frames_walk[] = {
	{ ai_walk, 2.79f, widow_step },
	{ ai_walk, 2.77f },
	{ ai_walk, 3.53f },
	{ ai_walk, 3.97f },
	{ ai_walk, 4.13f }, // 5
	{ ai_walk, 4.09f },
	{ ai_walk, 3.84f },
	{ ai_walk, 3.62f, widow_step },
	{ ai_walk, 3.29f },
	{ ai_walk, 6.08f }, // 10
	{ ai_walk, 6.94f },
	{ ai_walk, 5.73f },
	{ ai_walk, 2.85f }
};

static mframe_t widow_frames_run[] = {
	{ ai_run, 2.79f, widow_step },
	{ ai_run, 2.77f },
	{ ai_run, 3.53f },
	{ ai_run, 3.97f },
	{ ai_run, 4.13f }, // 5
	{ ai_run, 4.09f },
	{ ai_run, 3.84f },
	{ ai_run, 3.62f, widow_step },
	{ ai_run, 3.29f },
	{ ai_run, 6.08f }, // 10
	{ ai_run, 6.94f },
	{ ai_run, 5.73f },
	{ ai_run, 2.85f }
};

static mframe_t widow_frames_run_attack[] = {
	{ ai_charge, 13, widow_stepshoot },
	{ ai_charge, 11.72f, WidowBlaster },
	{ ai_charge, 18.04f, WidowBlaster },
	{ ai_charge, 14.58f, WidowBlaster },
	{ ai_charge, 13, widow_stepshoot }, // 5
	{ ai_charge, 12.12f, WidowBlaster },
	{ ai_charge, 19.63f, WidowBlaster },
	{ ai_charge, 11.37f, WidowBlaster }
};

static mframe_t widow_frames_attack_pre_blaster[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow_attack_blaster }
};

static mframe_t widow_frames_attack_blaster[] = {
	{ ai_charge, 0, widow_reattack_blaster }, // straight ahead
	{ ai_charge, 0, widow_reattack_blaster }, // 100 degrees right
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, // 50 degrees right
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, // straight
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster }, // 50 degrees left
	{ ai_charge, 0, widow_reattack_blaster },
	{ ai_charge, 0, widow_reattack_blaster } // 70 degrees left
};

static mframe_t widow_frames_attack_post_blaster[] = {
	{ ai_charge },
	{ ai_charge }
};

static mframe_t widow_frames_attack_post_blaster_r[] = {
	{ ai_charge, -2 },
	{ ai_charge, -10 },
	{ ai_charge, -2 },
	{ ai_charge },
	{ ai_charge, 0, widow_start_run_12 }
};

static mframe_t widow_frames_attack_post_blaster_l[] = {
	{ ai_charge },
	{ ai_charge, 14 },
	{ ai_charge, -2 },
	{ ai_charge, 10 },
	{ ai_charge, 10, widow_start_run_12 }
};

static mframe_t widow_frames_attack_pre_rail[] = {
	{ ai_charge, 0, widow_start_rail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow_attack_rail }
};

static mframe_t widow_frames_attack_rail[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, WidowSaveLoc },
	{ ai_charge, -10, WidowRail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow_rail_done }
};

static mframe_t widow_frames_attack_rail_r[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, WidowSaveLoc },
	{ ai_charge, -10, WidowRail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow_rail_done }
};

static mframe_t widow_frames_attack_rail_l[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, WidowSaveLoc },
	{ ai_charge, -10, WidowRail },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow_rail_done }
};

static mframe_t widow_frames_spawn[] = {
	{ ai_charge }, // 1
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow_start_spawn },
	{ ai_charge },						 // 5
	{ ai_charge, 0, WidowBlaster },		 // 6
	{ ai_charge, 0, widow_ready_spawn }, // 7
	{ ai_charge, 0, WidowBlaster },
	{ ai_charge, 0, WidowBlaster }, // 9
	{ ai_charge, 0, widow_spawn_check },
	{ ai_charge, 0, WidowBlaster }, // 11
	{ ai_charge, 0, WidowBlaster },
	{ ai_charge, 0, WidowBlaster }, // 13
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow_done_spawn }
};

static mframe_t widow_frames_pain_heavy[] = {
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

static mframe_t widow_frames_pain_light[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move }
};

static mframe_t widow_frames_death[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 5
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, spawn_out_start }, // 10
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 15
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 20
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 25
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 30
	{ ai_move, 0, spawn_out_do }
};

static mframe_t widow_frames_attack_kick[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, widow_attack_kick },
	{ ai_move }, // 5
	{ ai_move },
	{ ai_move },
	{ ai_move }
};

static mmove_t widow_move_stand = { FRAME_idle01, FRAME_idle11, widow_frames_stand, NULL };
static mmove_t widow_move_walk = { FRAME_walk01, FRAME_walk13, widow_frames_walk, NULL };
static mmove_t widow_move_run = { FRAME_walk01, FRAME_walk13, widow_frames_run, NULL };
static mmove_t widow_move_run_attack = { FRAME_run01, FRAME_run08, widow_frames_run_attack, widow_run };
static mmove_t widow_move_attack_pre_blaster = { FRAME_fired01, FRAME_fired02a, widow_frames_attack_pre_blaster, NULL };
static mmove_t widow_move_attack_blaster = { FRAME_fired02a, FRAME_fired20, widow_frames_attack_blaster, NULL };
static mmove_t widow_move_attack_post_blaster = { FRAME_fired21, FRAME_fired22, widow_frames_attack_post_blaster, widow_run };
static mmove_t widow_move_attack_post_blaster_r = { FRAME_transa01, FRAME_transa05, widow_frames_attack_post_blaster_r, NULL };
static mmove_t widow_move_attack_post_blaster_l = { FRAME_transb01, FRAME_transb05, widow_frames_attack_post_blaster_l, NULL };
static mmove_t widow_move_attack_pre_rail = { FRAME_transc01, FRAME_transc04, widow_frames_attack_pre_rail, NULL };
static mmove_t widow_move_attack_rail = { FRAME_firea01, FRAME_firea09, widow_frames_attack_rail, widow_run };
static mmove_t widow_move_attack_rail_r = { FRAME_fireb01, FRAME_fireb09, widow_frames_attack_rail_r, widow_run };
static mmove_t widow_move_attack_rail_l = { FRAME_firec01, FRAME_firec09, widow_frames_attack_rail_l, widow_run };
static mmove_t widow_move_spawn = { FRAME_spawn01, FRAME_spawn18, widow_frames_spawn, widow_run };
static mmove_t widow_move_pain_heavy = { FRAME_pain01, FRAME_pain13, widow_frames_pain_heavy, widow_run };
static mmove_t widow_move_pain_light = { FRAME_pain201, FRAME_pain203, widow_frames_pain_light, widow_run };
static mmove_t widow_move_death = { FRAME_death01, FRAME_death31, widow_frames_death, NULL };
static mmove_t widow_move_attack_kick = { FRAME_kick01, FRAME_kick08, widow_frames_attack_kick, widow_run };

static void widow_search (edict_t *self)
{
}

static void widow_sight (edict_t *self, edict_t *other)
{
	self->monsterinfo.fire_wait = 0;
}

// where its enemy is, -180 to 180 of straight ahead
static float target_angle (edict_t *self)
{
	vec3_t	target;
	float	enemy_yaw;

	VectorSubtract (self->s.origin, self->enemy->s.origin, target);
	enemy_yaw = self->s.angles[YAW] - vectoyaw2 (target);
	if (enemy_yaw < 0)
		enemy_yaw += 360.0;

	// this gets me 0 degrees = forward
	enemy_yaw -= 180.0;
	// positive is to right, negative to left

	return enemy_yaw;
}

// the frame its torso turns to, at its enemy; 0 past where it can turn
static int WidowTorso (edict_t *self)
{
	float enemy_yaw = target_angle (self);

	if (enemy_yaw >= 105)
	{
		self->monsterinfo.currentmove = &widow_move_attack_post_blaster_r;
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		return 0;
	}

	if (enemy_yaw <= -75.0)
	{
		self->monsterinfo.currentmove = &widow_move_attack_post_blaster_l;
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		return 0;
	}

	if (enemy_yaw >= 95)
		return FRAME_fired03;
	else if (enemy_yaw >= 85)
		return FRAME_fired04;
	else if (enemy_yaw >= 75)
		return FRAME_fired05;
	else if (enemy_yaw >= 65)
		return FRAME_fired06;
	else if (enemy_yaw >= 55)
		return FRAME_fired07;
	else if (enemy_yaw >= 45)
		return FRAME_fired08;
	else if (enemy_yaw >= 35)
		return FRAME_fired09;
	else if (enemy_yaw >= 25)
		return FRAME_fired10;
	else if (enemy_yaw >= 15)
		return FRAME_fired11;
	else if (enemy_yaw >= 5)
		return FRAME_fired12;
	else if (enemy_yaw >= -5)
		return FRAME_fired13;
	else if (enemy_yaw >= -15)
		return FRAME_fired14;
	else if (enemy_yaw >= -25)
		return FRAME_fired15;
	else if (enemy_yaw >= -35)
		return FRAME_fired16;
	else if (enemy_yaw >= -45)
		return FRAME_fired17;
	else if (enemy_yaw >= -55)
		return FRAME_fired18;
	else if (enemy_yaw >= -65)
		return FRAME_fired19;
	else if (enemy_yaw >= -75)
		return FRAME_fired20;

	return 0;
}

#define VARIANCE 15.0

static void WidowBlaster (edict_t *self)
{
	vec3_t	forward, right, target, vec, targ_angles;
	vec3_t	start;
	int		flashnum;
	int		effect;

	if (!self->enemy)
		return;

	shotsfired++;
	if (!(shotsfired % 4))
		effect = EF_BLASTER;
	else
		effect = 0;

	AngleVectors (self->s.angles, forward, right, NULL);
	if ((self->s.frame >= FRAME_spawn05) && (self->s.frame <= FRAME_spawn13))
	{
		// sweep
		flashnum = MZ2_WIDOW_BLASTER_SWEEP1 + self->s.frame - FRAME_spawn05;
		G_ProjectSource (self->s.origin, monster_flash_offset[flashnum], forward, right, start);
		VectorSubtract (self->enemy->s.origin, start, target);
		vectoangles2 (target, targ_angles);

		VectorCopy (self->s.angles, vec);

		vec[PITCH] += targ_angles[PITCH];
		vec[YAW] -= sweep_angles[flashnum - MZ2_WIDOW_BLASTER_SWEEP1];

		AngleVectors (vec, forward, NULL, NULL);
		monster_fire_blaster2 (self, start, forward, BLASTER2_DAMAGE * widow_damage_multiplier, 1000, flashnum, effect);
	}
	else if ((self->s.frame >= FRAME_fired02a) && (self->s.frame <= FRAME_fired20))
	{
		vec3_t	angles;
		float	aim_angle, target_angle;
		float	error;

		self->monsterinfo.aiflags |= AI_MANUAL_STEERING;

		self->monsterinfo.nextframe = WidowTorso (self);

		if (!self->monsterinfo.nextframe)
			self->monsterinfo.nextframe = self->s.frame;

		if (self->s.frame == FRAME_fired02a)
			flashnum = MZ2_WIDOW_BLASTER_0;
		else
			flashnum = MZ2_WIDOW_BLASTER_100 + self->s.frame - FRAME_fired03;

		G_ProjectSource (self->s.origin, monster_flash_offset[flashnum], forward, right, start);

		PredictAim (self, self->enemy, start, 1000, true, crandom () * 0.1, forward, NULL);

		// clamp it to within 10 degrees of the aiming angle (where she's facing)
		vectoangles2 (forward, angles);
		// give me 100 -> -70
		aim_angle = (float) (100 - (10 * (flashnum - MZ2_WIDOW_BLASTER_100)));
		if (aim_angle <= 0)
			aim_angle += 360;
		target_angle = self->s.angles[YAW] - angles[YAW];
		if (target_angle <= 0)
			target_angle += 360;

		error = aim_angle - target_angle;

		// positive error is to entity's left, aka positive direction in engine
		// unfortunately, I decided that for the aim_angle, positive was right.  *sigh*
		if (error > VARIANCE)
		{
			angles[YAW] = (self->s.angles[YAW] - aim_angle) + VARIANCE;
			AngleVectors (angles, forward, NULL, NULL);
		}
		else if (error < -VARIANCE)
		{
			angles[YAW] = (self->s.angles[YAW] - aim_angle) - VARIANCE;
			AngleVectors (angles, forward, NULL, NULL);
		}

		monster_fire_blaster2 (self, start, forward, BLASTER2_DAMAGE * widow_damage_multiplier, 1000, flashnum, effect);
	}
	else if ((self->s.frame >= FRAME_run01) && (self->s.frame <= FRAME_run08))
	{
		flashnum = MZ2_WIDOW_RUN_1 + self->s.frame - FRAME_run01;
		G_ProjectSource (self->s.origin, monster_flash_offset[flashnum], forward, right, start);

		VectorSubtract (self->enemy->s.origin, start, target);
		target[2] += self->enemy->viewheight;
		VectorNormalize (target);

		monster_fire_blaster2 (self, start, target, BLASTER2_DAMAGE * widow_damage_multiplier, 1000, flashnum, effect);
	}
}

// summon a stalker at each side: on its enemy, or in coop on another player
void Widow_SpawnStalkers (edict_t *self, vec3_t *points)
{
	vec3_t	f, r, u, startpoint, spawnpoint;
	edict_t	*ent, *designated_enemy;
	int		i;

	AngleVectors (self->s.angles, f, r, u);

	for (i = 0; i < 2; i++)
	{
		G_ProjectSource2 (self->s.origin, points[i], f, r, u, startpoint);

		if (FindSpawnPoint (startpoint, stalker_mins, stalker_maxs, spawnpoint, 64))
		{
			ent = CreateGroundMonster (spawnpoint, self->s.angles, stalker_mins, stalker_maxs, "monster_stalker", 256);

			if (!ent)
				continue;

			self->monsterinfo.monster_used++;
			ent->monsterinfo.commander = self;

			ent->nextthink = level.time;
			ent->think (ent);

			ent->monsterinfo.aiflags |= AI_SPAWNED_WIDOW | AI_DO_NOT_COUNT | AI_IGNORE_SHOTS;

			if (!coop->value)
			{
				designated_enemy = self->enemy;
			}
			else
			{
				designated_enemy = PickCoopTarget (ent);
				if (designated_enemy)
				{
					// try to avoid using my enemy
					if (designated_enemy == self->enemy)
					{
						designated_enemy = PickCoopTarget (ent);
						if (!designated_enemy)
							designated_enemy = self->enemy;
					}
				}
				else
					designated_enemy = self->enemy;
			}

			if (designated_enemy && (designated_enemy->inuse) && (designated_enemy->health > 0))
			{
				ent->enemy = designated_enemy;
				FoundTarget (ent);
				ent->monsterinfo.attack (ent);
			}
		}
	}
}

// and the glow of where they will be
void Widow_ReadyStalkers (edict_t *self, vec3_t *points)
{
	vec3_t	f, r, u, startpoint, spawnpoint;
	int		i;

	AngleVectors (self->s.angles, f, r, u);

	for (i = 0; i < 2; i++)
	{
		G_ProjectSource2 (self->s.origin, points[i], f, r, u, startpoint);
		if (FindSpawnPoint (startpoint, stalker_mins, stalker_maxs, spawnpoint, 64))
		{
			VectorAdd (spawnpoint, stalker_mins, spawnpoint);
			VectorAdd (spawnpoint, stalker_maxs, spawnpoint);
			SpawnGrow_Spawn (spawnpoint, 0);
		}
	}
}

static void widow_spawn_check (edict_t *self)
{
	WidowBlaster (self);
	Widow_SpawnStalkers (self, spawnpoints);
}

static void widow_ready_spawn (edict_t *self)
{
	WidowBlaster (self);
	Widow_ReadyStalkers (self, spawnpoints);
}

static void widow_step (edict_t *self)
{
	gi.sound (self, CHAN_BODY, gi.soundindex ("widow/bwstep3.wav"), 1, ATTN_NORM, 0);
}

static void widow_stepshoot (edict_t *self)
{
	gi.sound (self, CHAN_BODY, gi.soundindex ("widow/bwstep2.wav"), 1, ATTN_NORM, 0);
	WidowBlaster (self);
}

static void widow_start_run_12 (edict_t *self)
{
	self->monsterinfo.currentmove = &widow_move_run;
	self->monsterinfo.nextframe = FRAME_walk12;
}

static void WidowRail (edict_t *self)
{
	vec3_t	start;
	vec3_t	dir;
	vec3_t	forward, right;
	int		flash;

	AngleVectors (self->s.angles, forward, right, NULL);

	if (self->monsterinfo.currentmove == &widow_move_attack_rail_l)
		flash = MZ2_WIDOW_RAIL_LEFT;
	else if (self->monsterinfo.currentmove == &widow_move_attack_rail_r)
		flash = MZ2_WIDOW_RAIL_RIGHT;
	else
		flash = MZ2_WIDOW_RAIL;

	G_ProjectSource (self->s.origin, monster_flash_offset[flash], forward, right, start);

	// calc direction to where we targeted
	VectorSubtract (self->pos1, start, dir);
	VectorNormalize (dir);

	monster_fire_railgun (self, start, dir, WIDOW_RAIL_DAMAGE * widow_damage_multiplier, 100, flash);
	self->timestamp = level.time + RAIL_TIME;
}

static void WidowSaveLoc (edict_t *self)
{
	VectorCopy (self->enemy->s.origin, self->pos1); // save for aiming the shot
	self->pos1[2] += self->enemy->viewheight;
}

static void widow_start_rail (edict_t *self)
{
	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
}

static void widow_rail_done (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
}

static void widow_attack_rail (edict_t *self)
{
	float enemy_angle;

	enemy_angle = target_angle (self);

	if (enemy_angle < -15)
		self->monsterinfo.currentmove = &widow_move_attack_rail_l;
	else if (enemy_angle > 15)
		self->monsterinfo.currentmove = &widow_move_attack_rail_r;
	else
		self->monsterinfo.currentmove = &widow_move_attack_rail;
}

static void widow_start_spawn (edict_t *self)
{
	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
}

static void widow_done_spawn (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
}

// beamed out, as its second form comes in
static void spawn_out_start (edict_t *self)
{
	vec3_t startpoint, f, r, u;

	AngleVectors (self->s.angles, f, r, u);

	G_ProjectSource2 (self->s.origin, beameffects[0], f, r, u, startpoint);
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_WIDOWBEAMOUT);
	gi.WriteShort (20001);
	gi.WritePosition (startpoint);
	gi.multicast (startpoint, MULTICAST_ALL);

	G_ProjectSource2 (self->s.origin, beameffects[1], f, r, u, startpoint);
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_WIDOWBEAMOUT);
	gi.WriteShort (20002);
	gi.WritePosition (startpoint);
	gi.multicast (startpoint, MULTICAST_ALL);

	gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/bwidowbeamout.wav"), 1, ATTN_NORM, 0);
}

static void spawn_out_do (edict_t *self)
{
	vec3_t startpoint, f, r, u;

	AngleVectors (self->s.angles, f, r, u);
	G_ProjectSource2 (self->s.origin, beameffects[0], f, r, u, startpoint);
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_WIDOWSPLASH);
	gi.WritePosition (startpoint);
	gi.multicast (startpoint, MULTICAST_ALL);

	G_ProjectSource2 (self->s.origin, beameffects[1], f, r, u, startpoint);
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_WIDOWSPLASH);
	gi.WritePosition (startpoint);
	gi.multicast (startpoint, MULTICAST_ALL);

	VectorCopy (self->s.origin, startpoint);
	startpoint[2] += 36;
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BOSSTPORT);
	gi.WritePosition (startpoint);
	gi.multicast (startpoint, MULTICAST_PHS);

	Widowlegs_Spawn (self->s.origin, self->s.angles);

	G_FreeEdict (self);
}

static void widow_attack_kick (edict_t *self)
{
	vec3_t aim = { 100, 0, 4 };

	if (self->enemy->groundentity)
		fire_hit_rr (self, aim, 50 + (rand () % 6), 500);
	else // not as much kick if they're in the air .. makes it harder to land on her head
		fire_hit_rr (self, aim, 50 + (rand () % 6), 250);
}

static void widow_stand (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, gi.soundindex ("widow/laugh.wav"), 1, ATTN_NORM, 0);
	self->monsterinfo.currentmove = &widow_move_stand;
}

static void widow_run (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &widow_move_stand;
	else
		self->monsterinfo.currentmove = &widow_move_run;
}

static void widow_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &widow_move_walk;
}

static void widow_pre_rail (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_rail, 1, ATTN_NORM, 0);
	self->monsterinfo.currentmove = &widow_move_attack_pre_rail;
}

static void widow_attack (edict_t *self)
{
	float		luck;
	qboolean	rail_frames = false, blaster_frames = false, blocked = false, anger = false;

	self->movetarget = NULL;

	if (self->monsterinfo.aiflags & AI_BLOCKED)
	{
		blocked = true;
		self->monsterinfo.aiflags &= ~AI_BLOCKED;
	}

	if (self->monsterinfo.aiflags & AI_TARGET_ANGER)
	{
		anger = true;
		self->monsterinfo.aiflags &= ~AI_TARGET_ANGER;
	}

	if ((!self->enemy) || (!self->enemy->inuse))
		return;

	if (self->bad_area)
	{
		if ((random () < 0.1) || (level.time < self->timestamp))
			self->monsterinfo.currentmove = &widow_move_attack_pre_blaster;
		else
			widow_pre_rail (self);
		return;
	}

	// frames FRAME_walk13, FRAME_walk01, FRAME_walk02, FRAME_walk03 are rail gun start frames
	// frames FRAME_walk09, FRAME_walk10, FRAME_walk11, FRAME_walk12 are spawn & blaster start frames

	if ((self->s.frame == FRAME_walk13) || ((self->s.frame >= FRAME_walk01) && (self->s.frame <= FRAME_walk03)))
		rail_frames = true;

	if ((self->s.frame >= FRAME_walk09) && (self->s.frame <= FRAME_walk12))
		blaster_frames = true;

	WidowCalcSlots (self);

	// if we can't see the target, spawn stuff regardless of frame
	if ((self->monsterinfo.attack_state == AS_BLIND) && (M_SlotsLeft (self) >= 2))
	{
		self->monsterinfo.currentmove = &widow_move_spawn;
		return;
	}

	// accept bias towards spawning regardless of frame
	if (blocked && (M_SlotsLeft (self) >= 2))
	{
		self->monsterinfo.currentmove = &widow_move_spawn;
		return;
	}

	if ((realrange (self, self->enemy) > 300) && (!anger) && (random () < 0.5) && (!blocked))
	{
		self->monsterinfo.currentmove = &widow_move_run_attack;
		return;
	}

	if (blaster_frames)
	{
		if (M_SlotsLeft (self) >= 2)
		{
			self->monsterinfo.currentmove = &widow_move_spawn;
			return;
		}
		else if (self->monsterinfo.fire_wait + BLASTER_TIME <= level.time)
		{
			self->monsterinfo.currentmove = &widow_move_attack_pre_blaster;
			return;
		}
	}

	if (rail_frames)
	{
		if (!(level.time < self->timestamp))
			widow_pre_rail (self);
	}

	if ((rail_frames) || (blaster_frames))
		return;

	luck = random ();
	if (M_SlotsLeft (self) >= 2)
	{
		if ((luck <= 0.40) && (self->monsterinfo.fire_wait + BLASTER_TIME <= level.time))
			self->monsterinfo.currentmove = &widow_move_attack_pre_blaster;
		else if ((luck <= 0.7) && !(level.time < self->timestamp))
			widow_pre_rail (self);
		else
			self->monsterinfo.currentmove = &widow_move_spawn;
	}
	else
	{
		if (level.time < self->timestamp)
			self->monsterinfo.currentmove = &widow_move_attack_pre_blaster;
		else if ((luck <= 0.50) || (level.time + BLASTER_TIME >= self->monsterinfo.fire_wait))
			widow_pre_rail (self);
		else // holdout to blaster
			self->monsterinfo.currentmove = &widow_move_attack_pre_blaster;
	}
}

static void widow_attack_blaster (edict_t *self)
{
	self->monsterinfo.fire_wait = level.time + 1 + random () * 2;
	self->monsterinfo.currentmove = &widow_move_attack_blaster;
	self->monsterinfo.nextframe = WidowTorso (self);
}

static void widow_reattack_blaster (edict_t *self)
{
	WidowBlaster (self);

	// if WidowBlaster bailed us out of the frames, just bail
	if ((self->monsterinfo.currentmove == &widow_move_attack_post_blaster_l) ||
		(self->monsterinfo.currentmove == &widow_move_attack_post_blaster_r))
		return;

	// if we're not done with the attack, don't leave the sequence
	if (self->monsterinfo.fire_wait >= level.time)
		return;

	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

	self->monsterinfo.currentmove = &widow_move_attack_post_blaster;
}

static void widow_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 5;

	if (damage < 15)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NONE, 0);
	else if (damage < 75)
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NONE, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain3, 1, ATTN_NONE, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	self->monsterinfo.fire_wait = 0;

	if (damage >= 15)
	{
		if (damage < 75)
		{
			if ((skill->value < 3) && (random () < (0.6 - (0.2 * skill->value))))
			{
				self->monsterinfo.currentmove = &widow_move_pain_light;
				self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
			}
		}
		else
		{
			if ((skill->value < 3) && (random () < (0.75 - (0.1 * skill->value))))
			{
				self->monsterinfo.currentmove = &widow_move_pain_heavy;
				self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
			}
		}
	}
}

static void widow_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static void widow_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_NO;
	self->count = 0;
	self->monsterinfo.quad_time = 0;
	self->monsterinfo.double_time = 0;
	self->monsterinfo.invincible_time = 0;
	self->monsterinfo.currentmove = &widow_move_death;
}

static void widow_melee (edict_t *self)
{
	self->monsterinfo.currentmove = &widow_move_attack_kick;
}

static void WidowGoinQuad (edict_t *self, float time)
{
	self->monsterinfo.quad_time = time;
	widow_damage_multiplier = 4;
}

static void WidowDouble (edict_t *self, float time)
{
	self->monsterinfo.double_time = time;
	widow_damage_multiplier = 2;
}

static void WidowPent (edict_t *self, float time)
{
	self->monsterinfo.invincible_time = time;
}

static void WidowPowerArmor (edict_t *self)
{
	self->monsterinfo.power_armor_type = POWER_ARMOR_SHIELD;
	// I don't like this, but it works
	if (self->monsterinfo.power_armor_power <= 0)
		self->monsterinfo.power_armor_power += 250 * skill->value;
}

// its answer to a player's powerup, harder the harder the skill
static void WidowRespondPowerup (edict_t *self, edict_t *other)
{
	if (!other || !other->client)
		return;

	if (other->s.effects & EF_QUAD)
	{
		if (skill->value == 1)
			WidowDouble (self, other->client->quad_framenum * FRAMETIME);
		else if (skill->value == 2)
			WidowGoinQuad (self, other->client->quad_framenum * FRAMETIME);
		else if (skill->value == 3)
		{
			WidowGoinQuad (self, other->client->quad_framenum * FRAMETIME);
			WidowPowerArmor (self);
		}
	}
	else if (other->s.effects & EF_DOUBLE)
	{
		if (skill->value == 2)
			WidowDouble (self, other->client->double_framenum * FRAMETIME);
		else if (skill->value == 3)
		{
			WidowDouble (self, other->client->double_framenum * FRAMETIME);
			WidowPowerArmor (self);
		}
	}
	else
		widow_damage_multiplier = 1;

	if (other->s.effects & EF_PENT)
	{
		if (skill->value == 1)
			WidowPowerArmor (self);
		else if (skill->value == 2)
			WidowPent (self, other->client->invincible_framenum * FRAMETIME);
		else if (skill->value == 3)
		{
			WidowPent (self, other->client->invincible_framenum * FRAMETIME);
			WidowPowerArmor (self);
		}
	}
}

// in coop, a pent's answered first, then a quad, then a double
static qboolean WidowPowerups_coop (edict_t *self, int effect)
{
	edict_t	*ent;
	int		player;

	for (player = 1; player <= game.maxclients; player++)
	{
		ent = &g_edicts[player];
		if (!ent->inuse)
			continue;
		if (!ent->client)
			continue;
		if (ent->s.effects & effect)
		{
			WidowRespondPowerup (self, ent);
			return true;
		}
	}
	return false;
}

void WidowPowerups (edict_t *self)
{
	if (!coop->value)
	{
		WidowRespondPowerup (self, self->enemy);
	}
	else
	{
		if (WidowPowerups_coop (self, EF_PENT))
			return;
		if (WidowPowerups_coop (self, EF_QUAD))
			return;
		WidowPowerups_coop (self, EF_DOUBLE);
	}
}

static qboolean Widow_CheckAttack (edict_t *self)
{
	if (!self->enemy)
		return false;

	WidowPowerups (self);

	if (self->monsterinfo.currentmove == &widow_move_run)
	{
		// if we're in run, make sure we're in a good frame for attacking before doing anything else
		// frames 1,2,3,9,10,11,13 good to fire
		switch (self->s.frame)
		{
		case FRAME_walk04:
		case FRAME_walk05:
		case FRAME_walk06:
		case FRAME_walk07:
		case FRAME_walk08:
		case FRAME_walk12:
			return false;
		default:
			break;
		}
	}

	// give a LARGE bias to spawning things when we have room
	// use AI_BLOCKED as a signal to attack to spawn
	if ((random () < 0.8) && (M_SlotsLeft (self) >= 2) && (realrange (self, self->enemy) > 150))
	{
		self->monsterinfo.aiflags |= AI_BLOCKED;
		self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	return M_CheckAttack_Base (self, 0.4, 0.8, 0.7, 0.6, 0.5, 0);
}

static qboolean widow_blocked (edict_t *self, float dist)
{
	// if we get blocked while we're in our run/attack mode, turn on a meaningless (in this context)AI flag,
	// and call attack to get a new attack sequence.  make sure to turn it off when we're done.
	//
	// I'm using AI_TARGET_ANGER for this purpose

	if (self->monsterinfo.currentmove == &widow_move_run_attack)
	{
		self->monsterinfo.aiflags |= AI_TARGET_ANGER;
		if (self->monsterinfo.checkattack (self))
			self->monsterinfo.attack (self);
		else
			self->monsterinfo.run (self);
		return true;
	}

	return false;
}

// how many stalkers it may have out, by skill, more in coop
void WidowCalcSlots (edict_t *self)
{
	switch ((int) skill->value)
	{
	case 0:
	case 1:
		self->monsterinfo.monster_slots = 3;
		break;
	case 2:
		self->monsterinfo.monster_slots = 4;
		break;
	case 3:
		self->monsterinfo.monster_slots = 6;
		break;
	default:
		self->monsterinfo.monster_slots = 3;
		break;
	}
	if (coop->value)
	{
		self->monsterinfo.monster_slots += skill->value * (CountPlayers () - 1);
		if (self->monsterinfo.monster_slots > 6)
			self->monsterinfo.monster_slots = 6;
	}
}

static void WidowPrecache (void)
{
	// cache in all of the stalker stuff, widow stuff, spawngro stuff, gibs
	gi.soundindex ("stalker/pain.wav");
	gi.soundindex ("stalker/death.wav");
	gi.soundindex ("stalker/sight.wav");
	gi.soundindex ("stalker/melee1.wav");
	gi.soundindex ("stalker/melee2.wav");
	gi.soundindex ("stalker/idle.wav");

	gi.soundindex ("tank/tnkatck3.wav");
	gi.modelindex ("models/objects/laser/tris.md2");

	gi.modelindex ("models/monsters/stalker/tris.md2");
	gi.modelindex ("models/items/spawngro3/tris.md2");
	gi.modelindex ("models/objects/gibs/sm_metal/tris.md2");
	gi.modelindex ("models/objects/gibs/gear/tris.md2");
	gi.modelindex ("models/monsters/blackwidow/gib1/tris.md2");
	gi.modelindex ("models/monsters/blackwidow/gib2/tris.md2");
	gi.modelindex ("models/monsters/blackwidow/gib3/tris.md2");
	gi.modelindex ("models/monsters/blackwidow/gib4/tris.md2");
	gi.modelindex ("models/monsters/blackwidow2/gib1/tris.md2");
	gi.modelindex ("models/monsters/blackwidow2/gib2/tris.md2");
	gi.modelindex ("models/monsters/blackwidow2/gib3/tris.md2");
	gi.modelindex ("models/monsters/blackwidow2/gib4/tris.md2");
	gi.modelindex ("models/monsters/legs/tris.md2");
	gi.soundindex ("misc/bwidowbeamout.wav");

	gi.soundindex ("misc/bigtele.wav");
	gi.soundindex ("widow/bwstep3.wav");
	gi.soundindex ("widow/bwstep2.wav");
	gi.soundindex ("widow/bwstep1.wav");
}

/*QUAKED monster_widow (1 .5 0) (-40 -40 0) (40 40 144) Ambush Trigger_Spawn Sight
 */
void SP_monster_widow (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}

	sound_pain1 = gi.soundindex ("widow/bw1pain1.wav");
	sound_pain2 = gi.soundindex ("widow/bw1pain2.wav");
	sound_pain3 = gi.soundindex ("widow/bw1pain3.wav");
	sound_rail = gi.soundindex ("gladiator/railgun.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/blackwidow/tris.md2");
	VectorSet (self->mins, -40, -40, 0);
	VectorSet (self->maxs, 40, 40, 144);

	self->health = 2000 + 1000 * skill->value;
	if (coop->value)
		self->health += 500 * skill->value;
	self->gib_health = -5000;
	self->mass = 1500;

	if (skill->value == 3)
	{
		self->monsterinfo.power_armor_type = POWER_ARMOR_SHIELD;
		self->monsterinfo.power_armor_power = 500;
	}

	self->yaw_speed = 30;

	self->flags |= FL_IMMUNE_LASER;
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	self->pain = widow_pain;
	self->die = widow_die;

	self->monsterinfo.melee = widow_melee;
	self->monsterinfo.stand = widow_stand;
	self->monsterinfo.walk = widow_walk;
	self->monsterinfo.run = widow_run;
	self->monsterinfo.attack = widow_attack;
	self->monsterinfo.search = widow_search;
	self->monsterinfo.checkattack = Widow_CheckAttack;
	self->monsterinfo.sight = widow_sight;
	self->monsterinfo.setskin = widow_setskin;
	self->monsterinfo.blocked = widow_blocked;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &widow_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	WidowPrecache ();
	WidowCalcSlots (self);
	widow_damage_multiplier = 1;

	walkmonster_start (self);
}
