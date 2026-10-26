/*
==============================================================================

The re-release's carrier, for its own maps (RR_Map), from
rerelease/rogue/m_rogue_carrier.cpp: it flies as its flyers do, keeping
its distance; spins its chainguns up and down, fires rockets out the back
at whoever is behind or under it, leads its rockets, summons its
reinforcements by slots, and gibs into pieces of its own.

==============================================================================
*/

#include "g_local.h"
#include "m_carrier.h"

#define CARRIER_ROCKET_TIME		2	// number of seconds between rocket shots
#define CARRIER_ROCKET_SPEED	750
#define RAIL_FIRE_TIME			3

qboolean infront (edict_t *self, edict_t *other);
qboolean inback (edict_t *self, edict_t *other);
qboolean below (edict_t *self, edict_t *other);

extern mmove_t	*flyer_move_attack3_rr, *flyer_move_kamikaze_rr;

static int	sound_pain1;
static int	sound_pain2;
static int	sound_pain3;
static int	sound_death;
static int	sound_sight;
static int	sound_rail;
static int	sound_spawn;
static int	sound_engine;
static int	sound_cg_down, sound_cg_loop, sound_cg_up;

static float	orig_yaw_speed;

static void carrier_run (edict_t *self);
static void carrier_dead (edict_t *self);
static void carrier_attack_mg (edict_t *self);
static void carrier_reattack_mg (edict_t *self);
static void carrier_attack_gren (edict_t *self);
static void carrier_reattack_gren (edict_t *self);
static void carrier_start_spawn (edict_t *self);
static void carrier_spawn_check (edict_t *self);
static void carrier_prep_spawn (edict_t *self);
static void carrier_ready_spawn (edict_t *self);
static void CarrierCoopCheck (edict_t *self);
static void CarrierSpool (edict_t *self);
static void CarrierMachineGun (edict_t *self);
static void CarrierGrenade (edict_t *self);
static void CarrierRocket (edict_t *self);
static void CarrierRail (edict_t *self);
static void CarrierSaveLoc (edict_t *self);

static void BossExplode (edict_t *self)
{
	BossExplode_rr (self);
}

// its chaingun's whirring, in place of its engine's while it fires
static void carrier_weapon_sound (edict_t *self, int sound)
{
	self->s.sound = sound ? sound : sound_engine;
}

static mframe_t carrier_frames_stand[] = {
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

static mframe_t carrier_frames_walk[] = {
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

static mframe_t carrier_frames_run[] = {
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck },
	{ ai_run, 6, CarrierCoopCheck }
};

static mframe_t carrier_frames_attack_pre_mg[] = {
	{ ai_charge, 4, CarrierSpool },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, carrier_attack_mg }
};

static mframe_t carrier_frames_attack_mg[] = {
	{ ai_charge, -2, CarrierMachineGun },
	{ ai_charge, -2, CarrierMachineGun },
	{ ai_charge, -2, carrier_reattack_mg }
};

static mframe_t carrier_frames_attack_post_mg[] = {
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck }
};

static mframe_t carrier_frames_attack_pre_gren[] = {
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, carrier_attack_gren }
};

static mframe_t carrier_frames_attack_gren[] = {
	{ ai_charge, -15, CarrierGrenade },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, carrier_reattack_gren }
};

static mframe_t carrier_frames_attack_post_gren[] = {
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck },
	{ ai_charge, 4, CarrierCoopCheck }
};

static mframe_t carrier_frames_attack_rocket[] = {
	{ ai_charge, 15, CarrierRocket }
};

static mframe_t carrier_frames_attack_rail[] = {
	{ ai_charge, 2, CarrierCoopCheck },
	{ ai_charge, 2, CarrierSaveLoc },
	{ ai_charge, 2, CarrierCoopCheck },
	{ ai_charge, -20, CarrierRail },
	{ ai_charge, 2, CarrierCoopCheck },
	{ ai_charge, 2, CarrierCoopCheck },
	{ ai_charge, 2, CarrierCoopCheck },
	{ ai_charge, 2, CarrierCoopCheck },
	{ ai_charge, 2, CarrierCoopCheck }
};

static mframe_t carrier_frames_spawn[] = {
	{ ai_charge, -2 },
	{ ai_charge, -2 },
	{ ai_charge, -2 },
	{ ai_charge, -2 },
	{ ai_charge, -2 },
	{ ai_charge, -2 },
	{ ai_charge, -2, carrier_prep_spawn },	// 7 - end of wind down
	{ ai_charge, -2, carrier_start_spawn }, // 8 - start of spawn
	{ ai_charge, -2, carrier_ready_spawn },
	{ ai_charge, -2 },
	{ ai_charge, -2 },
	{ ai_charge, -10, carrier_spawn_check }, // 12 - actual spawn
	{ ai_charge, -2 },	 // 13 - begin of wind down
	{ ai_charge, -2 },
	{ ai_charge, -2 },
	{ ai_charge, -2 },
	{ ai_charge, -2 },
	{ ai_charge, -2 } // 18 - end of wind down
};

static mframe_t carrier_frames_pain_heavy[] = {
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

static mframe_t carrier_frames_pain_light[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};

static mframe_t carrier_frames_death[] = {
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
	{ ai_move }
};

static mmove_t carrier_move_stand = { FRAME_search01, FRAME_search13, carrier_frames_stand, NULL };
static mmove_t carrier_move_walk = { FRAME_search01, FRAME_search13, carrier_frames_walk, NULL };
static mmove_t carrier_move_run = { FRAME_search01, FRAME_search13, carrier_frames_run, NULL };
static mmove_t carrier_move_attack_pre_mg = { FRAME_firea01, FRAME_firea08, carrier_frames_attack_pre_mg, NULL };
static mmove_t carrier_move_attack_mg = { FRAME_firea09, FRAME_firea11, carrier_frames_attack_mg, NULL };
static mmove_t carrier_move_attack_post_mg = { FRAME_firea12, FRAME_firea15, carrier_frames_attack_post_mg, carrier_run };
static mmove_t carrier_move_attack_pre_gren = { FRAME_fireb01, FRAME_fireb06, carrier_frames_attack_pre_gren, NULL };
static mmove_t carrier_move_attack_gren = { FRAME_fireb07, FRAME_fireb10, carrier_frames_attack_gren, NULL };
static mmove_t carrier_move_attack_post_gren = { FRAME_fireb11, FRAME_fireb16, carrier_frames_attack_post_gren, carrier_run };
static mmove_t carrier_move_attack_rocket = { FRAME_fireb01, FRAME_fireb01, carrier_frames_attack_rocket, carrier_run };
static mmove_t carrier_move_attack_rail = { FRAME_search01, FRAME_search09, carrier_frames_attack_rail, carrier_run };
static mmove_t carrier_move_spawn = { FRAME_spawn01, FRAME_spawn18, carrier_frames_spawn, carrier_run };
static mmove_t carrier_move_pain_heavy = { FRAME_death01, FRAME_death10, carrier_frames_pain_heavy, carrier_run };
static mmove_t carrier_move_pain_light = { FRAME_spawn01, FRAME_spawn04, carrier_frames_pain_light, carrier_run };
static mmove_t carrier_move_death = { FRAME_death01, FRAME_death16, carrier_frames_death, carrier_dead };

static void carrier_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

// code starts here
// fire rockets out the back at whoever is behind or under it
static void CarrierCoopCheck (edict_t *self)
{
	edict_t	*targets[MAX_CLIENTS];
	int		num_targets = 0, target, player;
	edict_t	*ent;
	trace_t	tr;

	// if we have recently fired, bail
	if (self->monsterinfo.fire_wait > level.time)
		return;

	// cycle through players
	for (player = 1; player <= game.maxclients && num_targets < MAX_CLIENTS; player++)
	{
		ent = &g_edicts[player];
		if (!ent->inuse)
			continue;
		if (!ent->client)
			continue;
		if (inback (self, ent) || below (self, ent))
		{
			tr = gi.trace (self->s.origin, NULL, NULL, ent->s.origin, self, MASK_SOLID);
			if (tr.fraction == 1.0)
				targets[num_targets++] = ent;
		}
	}

	if (!num_targets)
		return;

	// get a number from 0 to (num_targets-1)
	target = rand () % num_targets;

	// make sure to prevent rapid fire rockets
	self->monsterinfo.fire_wait = level.time + CARRIER_ROCKET_TIME;

	// save off the real enemy
	ent = self->enemy;
	// set the new guy as temporary enemy
	self->enemy = targets[target];
	CarrierRocket (self);
	// put the real enemy back
	self->enemy = ent;
}

static void CarrierGrenade (edict_t *self)
{
	vec3_t	start;
	vec3_t	forward, right, up;
	vec3_t	aim;
	int		flash_number;
	float	direction; // from lower left to upper right, or lower right to upper left
	float	spreadR, spreadU;
	int		mytime;

	CarrierCoopCheck (self);

	if (!self->enemy)
		return;

	if (random () < 0.5)
		direction = -1.0;
	else
		direction = 1.0;

	mytime = (int) ((level.time - self->timestamp) / 0.4);

	if (mytime == 0)
	{
		spreadR = 0.15 * direction;
		spreadU = 0.1 - 0.1 * direction;
	}
	else if (mytime == 1)
	{
		spreadR = 0;
		spreadU = 0.1;
	}
	else if (mytime == 2)
	{
		spreadR = -0.15 * direction;
		spreadU = 0.1 - -0.1 * direction;
	}
	else if (mytime == 3)
	{
		spreadR = 0;
		spreadU = 0.1;
	}
	else
	{
		// error, shoot straight
		spreadR = 0;
		spreadU = 0;
	}

	AngleVectors (self->s.angles, forward, right, up);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_CARRIER_GRENADE], forward, right, start);

	VectorSubtract (self->enemy->s.origin, start, aim);
	VectorNormalize (aim);

	VectorMA (aim, spreadR, right, aim);
	VectorMA (aim, spreadU, up, aim);

	if (aim[2] > 0.15)
		aim[2] = 0.15;
	else if (aim[2] < -0.5)
		aim[2] = -0.5;

	flash_number = MZ2_GUNNER_GRENADE_1;
	fire_grenade_adjust (self, start, aim, 50, 600, 2.5, 90, crandom () * 10, 200 + crandom () * 10);
	monster_muzzleflash (self, start, flash_number);
}

static void CarrierPredictiveRocket (edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	dir;

	AngleVectors (self->s.angles, forward, right, NULL);

	// 1
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_CARRIER_ROCKET_1], forward, right, start);
	PredictAim (self, self->enemy, start, CARRIER_ROCKET_SPEED, false, -0.3, dir, NULL);
	monster_fire_rocket (self, start, dir, 50, CARRIER_ROCKET_SPEED, MZ2_CARRIER_ROCKET_1);

	// 2
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_CARRIER_ROCKET_2], forward, right, start);
	PredictAim (self, self->enemy, start, CARRIER_ROCKET_SPEED, false, -0.15, dir, NULL);
	monster_fire_rocket (self, start, dir, 50, CARRIER_ROCKET_SPEED, MZ2_CARRIER_ROCKET_2);

	// 3
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_CARRIER_ROCKET_3], forward, right, start);
	PredictAim (self, self->enemy, start, CARRIER_ROCKET_SPEED, false, 0, dir, NULL);
	monster_fire_rocket (self, start, dir, 50, CARRIER_ROCKET_SPEED, MZ2_CARRIER_ROCKET_3);

	// 4
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_CARRIER_ROCKET_4], forward, right, start);
	PredictAim (self, self->enemy, start, CARRIER_ROCKET_SPEED, false, 0.15, dir, NULL);
	monster_fire_rocket (self, start, dir, 50, CARRIER_ROCKET_SPEED, MZ2_CARRIER_ROCKET_4);
}

// one of its four spread rockets, aimed so much to the right
static void CarrierRocket1 (edict_t *self, vec3_t forward, vec3_t right, int flash, float drop, float spread)
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

static void CarrierRocket (edict_t *self)
{
	vec3_t	forward, right;

	if (self->enemy)
	{
		if (self->enemy->client && random () < 0.5)
		{
			CarrierPredictiveRocket (self);
			return;
		}
	}
	else
		return;

	AngleVectors (self->s.angles, forward, right, NULL);

	CarrierRocket1 (self, forward, right, MZ2_CARRIER_ROCKET_1, 15, 0.4);
	CarrierRocket1 (self, forward, right, MZ2_CARRIER_ROCKET_2, 0, 0.025);
	CarrierRocket1 (self, forward, right, MZ2_CARRIER_ROCKET_3, 0, -0.025);
	CarrierRocket1 (self, forward, right, MZ2_CARRIER_ROCKET_4, 15, -0.4);
}

static void carrier_firebullet (edict_t *self, int flashnum)
{
	vec3_t	forward, right, start;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[flashnum], forward, right, start);
	PredictAim (self, self->enemy, start, 0, true, -0.3, forward, NULL);
	monster_fire_bullet (self, start, forward, 6, 4, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, flashnum);
}

static void carrier_firebullet_right (edict_t *self)
{
	// if we're in manual steering mode, it means we're leaning down .. use the lower shot
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
		carrier_firebullet (self, MZ2_CARRIER_MACHINEGUN_R2);
	else
		carrier_firebullet (self, MZ2_CARRIER_MACHINEGUN_R1);
}

static void carrier_firebullet_left (edict_t *self)
{
	// if we're in manual steering mode, it means we're leaning down .. use the lower shot
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
		carrier_firebullet (self, MZ2_CARRIER_MACHINEGUN_L2);
	else
		carrier_firebullet (self, MZ2_CARRIER_MACHINEGUN_L1);
}

static void CarrierMachineGun (edict_t *self)
{
	CarrierCoopCheck (self);
	if (self->enemy)
		carrier_firebullet_left (self);
	if (self->enemy)
		carrier_firebullet_right (self);
}

static void CarrierSpawn (edict_t *self)
{
	vec3_t	f, r, offset, startpoint, spawnpoint;
	edict_t	*ent;
	int		which;

	VectorSet (offset, 105, 0, -58); // real distance needed is (sqrt (56*56*2) + sqrt(16*16*2)) or 101.8
	AngleVectors (self->s.angles, f, r, NULL);

	G_ProjectSource (self->s.origin, offset, f, r, startpoint);

	if (self->monsterinfo.chosen_reinforcements[0] == 255)
		return;

	which = self->monsterinfo.chosen_reinforcements[0];

	if (FindSpawnPoint (startpoint, self->monsterinfo.reinforcements[which].mins, self->monsterinfo.reinforcements[which].maxs, spawnpoint, 32))
	{
		ent = CreateFlyMonster (spawnpoint, self->s.angles, self->monsterinfo.reinforcements[which].mins,
			self->monsterinfo.reinforcements[which].maxs, self->monsterinfo.reinforcements[which].classname);

		if (!ent)
			return;

		gi.sound (self, CHAN_BODY, sound_spawn, 1, ATTN_NONE, 0);

		ent->nextthink = level.time;
		ent->think (ent);

		// this game's ai_charge circles only those it marks
		ent->monsterinfo.aiflags |= AI_SPAWNED_CARRIER | AI_DO_NOT_COUNT | AI_IGNORE_SHOTS | AI_CIRCLE_STRAFE;
		ent->monsterinfo.commander = self;
		ent->monsterinfo.monster_slots = self->monsterinfo.reinforcements[which].strength;
		self->monsterinfo.monster_used += self->monsterinfo.reinforcements[which].strength;

		if (self->enemy->inuse && self->enemy->health > 0)
		{
			ent->enemy = self->enemy;
			FoundTarget (ent);

			if (!strcmp (ent->classname, "monster_kamikaze"))
			{
				ent->monsterinfo.lefty = false;
				ent->monsterinfo.attack_state = AS_STRAIGHT;
				ent->monsterinfo.currentmove = flyer_move_kamikaze_rr;
				ent->monsterinfo.aiflags |= AI_CHARGING;
				ent->owner = self;
			}
			else if (!strcmp (ent->classname, "monster_flyer"))
			{
				ent->monsterinfo.lefty = rand () & 1;
				ent->monsterinfo.attack_state = AS_SLIDING;
				ent->monsterinfo.currentmove = flyer_move_attack3_rr;
			}
		}
	}
}

static void carrier_prep_spawn (edict_t *self)
{
	CarrierCoopCheck (self);
	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
	self->timestamp = level.time;
	self->yaw_speed = 10;
}

static void carrier_spawn_check (edict_t *self)
{
	CarrierCoopCheck (self);
	CarrierSpawn (self);

	if (level.time > self->timestamp + 2.0) // 0.5 seconds per flyer.  this gets three
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		self->yaw_speed = orig_yaw_speed;
	}
	else
		self->monsterinfo.nextframe = FRAME_spawn08;
}

static void carrier_ready_spawn (edict_t *self)
{
	float	current_yaw;
	vec3_t	offset, f, r, startpoint, spawnpoint;
	int		which;

	CarrierCoopCheck (self);

	current_yaw = anglemod (self->s.angles[YAW]);

	if (fabs (current_yaw - self->ideal_yaw) > 0.1)
	{
		self->monsterinfo.aiflags |= AI_HOLD_FRAME;
		self->timestamp += FRAMETIME;
		return;
	}

	self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

	if (!M_PickReinforcements (self, 1))
		return;

	which = self->monsterinfo.chosen_reinforcements[0];

	VectorSet (offset, 105, 0, -58);
	AngleVectors (self->s.angles, f, r, NULL);
	G_ProjectSource (self->s.origin, offset, f, r, startpoint);

	if (FindSpawnPoint (startpoint, self->monsterinfo.reinforcements[which].mins, self->monsterinfo.reinforcements[which].maxs, spawnpoint, 32))
	{
		VectorAdd (spawnpoint, self->monsterinfo.reinforcements[which].mins, spawnpoint);
		VectorAdd (spawnpoint, self->monsterinfo.reinforcements[which].maxs, spawnpoint);
		SpawnGrow_Spawn (spawnpoint, 0);
	}
}

static void carrier_start_spawn (edict_t *self)
{
	int		mytime;
	float	enemy_yaw;
	vec3_t	temp;

	CarrierCoopCheck (self);
	if (!orig_yaw_speed)
		orig_yaw_speed = self->yaw_speed;

	if (!self->enemy)
		return;

	mytime = (int) ((level.time - self->timestamp) / 0.5);

	VectorSubtract (self->enemy->s.origin, self->s.origin, temp);
	enemy_yaw = vectoyaw2 (temp);

	// note that the offsets are based on a forward of 105 from the end angle
	if (mytime == 0)
		self->ideal_yaw = anglemod (enemy_yaw - 30);
	else if (mytime == 1)
		self->ideal_yaw = anglemod (enemy_yaw);
	else if (mytime == 2)
		self->ideal_yaw = anglemod (enemy_yaw + 30);
}

static void CarrierSpool (edict_t *self)
{
	CarrierCoopCheck (self);
	gi.sound (self, CHAN_BODY, sound_cg_up, 1, 0.5, 0);

	carrier_weapon_sound (self, sound_cg_loop);
}

static void CarrierRail (edict_t *self)
{
	vec3_t	start;
	vec3_t	dir;
	vec3_t	forward, right;

	CarrierCoopCheck (self);
	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_CARRIER_RAILGUN], forward, right, start);

	// calc direction to where we targeted
	VectorSubtract (self->pos1, start, dir);
	VectorNormalize (dir);

	monster_fire_railgun (self, start, dir, 50, 100, MZ2_CARRIER_RAILGUN);
	self->monsterinfo.attack_finished = level.time + RAIL_FIRE_TIME;
}

static void CarrierSaveLoc (edict_t *self)
{
	CarrierCoopCheck (self);
	VectorCopy (self->enemy->s.origin, self->pos1); // save for aiming the shot
	self->pos1[2] += self->enemy->viewheight;
}

static void carrier_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &carrier_move_stand;
}

static void carrier_run (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &carrier_move_stand;
	else
		self->monsterinfo.currentmove = &carrier_move_run;
}

static void carrier_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &carrier_move_walk;
}

static void carrier_rail_start (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_rail, 1, ATTN_NORM, 0);
	self->monsterinfo.currentmove = &carrier_move_attack_rail;
}

static void carrier_attack (edict_t *self)
{
	vec3_t		vec;
	float		range, luck;
	qboolean	enemy_inback, enemy_infront, enemy_below;

	self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

	if (!self->enemy || !self->enemy->inuse)
		return;

	enemy_inback = inback (self, self->enemy);
	enemy_infront = infront (self, self->enemy);
	enemy_below = below (self, self->enemy);

	if (self->bad_area)
	{
		if (enemy_inback || enemy_below)
			self->monsterinfo.currentmove = &carrier_move_attack_rocket;
		else if (random () < 0.1 || level.time < self->monsterinfo.attack_finished)
			self->monsterinfo.currentmove = &carrier_move_attack_pre_mg;
		else
			carrier_rail_start (self);
		return;
	}

	if (self->monsterinfo.attack_state == AS_BLIND)
	{
		self->monsterinfo.currentmove = &carrier_move_spawn;
		return;
	}

	if (!enemy_inback && !enemy_infront && !enemy_below) // to side and not under
	{
		if (random () < 0.1 || level.time < self->monsterinfo.attack_finished)
			self->monsterinfo.currentmove = &carrier_move_attack_pre_mg;
		else
			carrier_rail_start (self);
		return;
	}

	if (enemy_infront)
	{
		VectorSubtract (self->enemy->s.origin, self->s.origin, vec);
		range = VectorLength (vec);
		if (range <= 125)
		{
			if (random () < 0.8 || level.time < self->monsterinfo.attack_finished)
				self->monsterinfo.currentmove = &carrier_move_attack_pre_mg;
			else
				carrier_rail_start (self);
		}
		else if (range < 600)
		{
			luck = random ();
			if (M_SlotsLeft (self) > 2)
			{
				if (luck <= 0.20)
					self->monsterinfo.currentmove = &carrier_move_attack_pre_mg;
				else if (luck <= 0.40)
					self->monsterinfo.currentmove = &carrier_move_attack_pre_gren;
				else if (luck <= 0.7 && !(level.time < self->monsterinfo.attack_finished))
					carrier_rail_start (self);
				else
					self->monsterinfo.currentmove = &carrier_move_spawn;
			}
			else
			{
				if (luck <= 0.30)
					self->monsterinfo.currentmove = &carrier_move_attack_pre_mg;
				else if (luck <= 0.65)
					self->monsterinfo.currentmove = &carrier_move_attack_pre_gren;
				else if (level.time >= self->monsterinfo.attack_finished)
					carrier_rail_start (self);
				else
					self->monsterinfo.currentmove = &carrier_move_attack_pre_mg;
			}
		}
		else // won't use grenades at this range
		{
			luck = random ();
			if (M_SlotsLeft (self) > 2)
			{
				if (luck < 0.3)
					self->monsterinfo.currentmove = &carrier_move_attack_pre_mg;
				else if (luck < 0.65 && !(level.time < self->monsterinfo.attack_finished))
				{
					VectorCopy (self->enemy->s.origin, self->pos1); // save for aiming the shot
					self->pos1[2] += self->enemy->viewheight;
					carrier_rail_start (self);
				}
				else
					self->monsterinfo.currentmove = &carrier_move_spawn;
			}
			else
			{
				if (luck < 0.45 || level.time < self->monsterinfo.attack_finished)
					self->monsterinfo.currentmove = &carrier_move_attack_pre_mg;
				else
					carrier_rail_start (self);
			}
		}
	}
	else if (enemy_below || enemy_inback)
	{
		self->monsterinfo.currentmove = &carrier_move_attack_rocket;
	}
}

static void carrier_attack_mg (edict_t *self)
{
	CarrierCoopCheck (self);
	self->monsterinfo.currentmove = &carrier_move_attack_mg;
	self->monsterinfo.melee_debounce_time = level.time + 1.2 + random () * 0.8;
}

static void carrier_reattack_mg (edict_t *self)
{
	CarrierMachineGun (self);

	CarrierCoopCheck (self);
	if (visible (self, self->enemy) && infront (self, self->enemy))
	{
		if (random () < 0.6)
		{
			self->monsterinfo.melee_debounce_time += 0.25 + random () * 0.25;
			self->monsterinfo.currentmove = &carrier_move_attack_mg;
			return;
		}
		else if (self->monsterinfo.melee_debounce_time > level.time)
		{
			self->monsterinfo.currentmove = &carrier_move_attack_mg;
			return;
		}
	}

	self->monsterinfo.currentmove = &carrier_move_attack_post_mg;
	carrier_weapon_sound (self, 0);
	gi.sound (self, CHAN_BODY, sound_cg_down, 1, 0.5, 0);
}

static void carrier_attack_gren (edict_t *self)
{
	CarrierCoopCheck (self);
	self->timestamp = level.time;
	self->monsterinfo.currentmove = &carrier_move_attack_gren;
}

static void carrier_reattack_gren (edict_t *self)
{
	CarrierCoopCheck (self);
	if (infront (self, self->enemy))
		if (self->timestamp + 1.3 > level.time) // four grenades
		{
			self->monsterinfo.currentmove = &carrier_move_attack_gren;
			return;
		}
	self->monsterinfo.currentmove = &carrier_move_attack_post_gren;
}

static void carrier_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	qboolean changed = false;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 5;

	if (damage < 10)
		gi.sound (self, CHAN_VOICE, sound_pain3, 1, ATTN_NONE, 0);
	else if (damage < 30)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NONE, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NONE, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	carrier_weapon_sound (self, 0);

	if (damage >= 10)
	{
		if (damage < 30)
		{
			if (meansOfDeath == MOD_CHAINFIST || random () < 0.5)
			{
				changed = true;
				self->monsterinfo.currentmove = &carrier_move_pain_light;
			}
		}
		else
		{
			self->monsterinfo.currentmove = &carrier_move_pain_heavy;
			changed = true;
		}
	}

	// if we changed frames, clean up our little messes
	if (changed)
	{
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		self->yaw_speed = orig_yaw_speed;
	}
}

static void carrier_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static void carrier_dead (edict_t *self)
{
	static gib_def_t gibs[] = {
		{ 2, "models/objects/gibs/sm_meat/tris.md2", GIB_ORGANIC },
		{ 3, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
		{ 1, "models/monsters/carrier/gibs/base.md2", GIB_SKINNED },
		{ 1, "models/monsters/carrier/gibs/chest.md2", GIB_SKINNED | GIB_UPRIGHT },
		{ 1, "models/monsters/carrier/gibs/gl.md2", GIB_SKINNED },
		{ 1, "models/monsters/carrier/gibs/lcg.md2", GIB_SKINNED | GIB_UPRIGHT },
		{ 1, "models/monsters/carrier/gibs/lwing.md2", GIB_SKINNED | GIB_UPRIGHT },
		{ 1, "models/monsters/carrier/gibs/rcg.md2", GIB_SKINNED | GIB_UPRIGHT },
		{ 1, "models/monsters/carrier/gibs/rwing.md2", GIB_SKINNED | GIB_UPRIGHT },
		{ 2, "models/monsters/carrier/gibs/spawner.md2", GIB_SKINNED },
		{ 2, "models/monsters/carrier/gibs/thigh.md2", GIB_SKINNED },
		{ 1, "models/monsters/carrier/gibs/head.md2", GIB_SKINNED | GIB_METALLIC | GIB_HEAD },
		{ 0, NULL }
	};

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1_BIG);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	self->s.sound = 0;
	self->s.skinnum /= 2;
	self->gravity = 1.0;

	ThrowGibs (self, 500, gibs);
}

static void carrier_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NONE, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_NO;
	self->count = 0;
	self->monsterinfo.currentmove = &carrier_move_death;
	VectorClear (self->velocity);
	// it hangs in the air as it blows up, its engine still going
	self->gravity *= 0.01;
	carrier_weapon_sound (self, 0);
}

static qboolean Carrier_CheckAttack (edict_t *self)
{
	qboolean enemy_infront = infront (self, self->enemy);
	qboolean enemy_inback = inback (self, self->enemy);
	qboolean enemy_below = below (self, self->enemy);

	// shoot out the back if appropriate
	if (enemy_inback || (!enemy_infront && enemy_below))
	{
		// this is using wait because the attack is supposed to be independent
		if (level.time >= self->monsterinfo.fire_wait)
		{
			self->monsterinfo.fire_wait = level.time + CARRIER_ROCKET_TIME;
			self->monsterinfo.attack (self);
			if (random () < 0.6)
				self->monsterinfo.attack_state = AS_SLIDING;
			else
				self->monsterinfo.attack_state = AS_STRAIGHT;
			return true;
		}
	}

	return M_CheckAttack_Base (self, 0.4, 0.8, 0.8, 0.8, 0.5, 0);
}

static void CarrierPrecache (void)
{
	gi.soundindex ("flyer/flysght1.wav");
	gi.soundindex ("flyer/flysrch1.wav");
	gi.soundindex ("flyer/flypain1.wav");
	gi.soundindex ("flyer/flypain2.wav");
	gi.soundindex ("flyer/flyatck2.wav");
	gi.soundindex ("flyer/flyatck1.wav");
	gi.soundindex ("flyer/flydeth1.wav");
	gi.soundindex ("flyer/flyatck3.wav");
	gi.soundindex ("flyer/flyidle1.wav");
	gi.soundindex ("weapons/rockfly.wav");
	gi.soundindex ("infantry/infatck1.wav");
	gi.soundindex ("gunner/gunatck3.wav");
	gi.soundindex ("weapons/grenlb1b.wav");
	gi.soundindex ("tank/rocket.wav");

	gi.modelindex ("models/monsters/flyer/tris.md2");
	gi.modelindex ("models/objects/rocket/tris.md2");
	gi.modelindex ("models/objects/debris2/tris.md2");
	gi.modelindex ("models/objects/grenade/tris.md2");
	gi.modelindex ("models/items/spawngro3/tris.md2");
	gi.modelindex ("models/objects/gibs/sm_metal/tris.md2");
	gi.modelindex ("models/objects/gibs/gear/tris.md2");
}

/*QUAKED monster_carrier (1 .5 0) (-56 -56 -44) (56 56 44) Ambush Trigger_Spawn Sight
 */
void SP_monster_carrier_rr (edict_t *self)
{
	sound_pain1 = gi.soundindex ("carrier/pain_md.wav");
	sound_pain2 = gi.soundindex ("carrier/pain_lg.wav");
	sound_pain3 = gi.soundindex ("carrier/pain_sm.wav");
	sound_death = gi.soundindex ("carrier/death.wav");
	sound_rail = gi.soundindex ("gladiator/railgun.wav");
	sound_sight = gi.soundindex ("carrier/sight.wav");
	sound_spawn = gi.soundindex ("medic_commander/monsterspawn1.wav");

	sound_cg_down = gi.soundindex ("weapons/chngnd1a.wav");
	sound_cg_loop = gi.soundindex ("weapons/chngnl1a.wav");
	sound_cg_up = gi.soundindex ("weapons/chngnu1a.wav");

	sound_engine = gi.soundindex ("bosshovr/bhvengn1.wav");
	self->s.sound = sound_engine;

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/carrier/tris.md2");

	gi.modelindex ("models/monsters/carrier/gibs/base.md2");
	gi.modelindex ("models/monsters/carrier/gibs/chest.md2");
	gi.modelindex ("models/monsters/carrier/gibs/gl.md2");
	gi.modelindex ("models/monsters/carrier/gibs/head.md2");
	gi.modelindex ("models/monsters/carrier/gibs/lcg.md2");
	gi.modelindex ("models/monsters/carrier/gibs/lwing.md2");
	gi.modelindex ("models/monsters/carrier/gibs/rcg.md2");
	gi.modelindex ("models/monsters/carrier/gibs/rwing.md2");
	gi.modelindex ("models/monsters/carrier/gibs/spawner.md2");
	gi.modelindex ("models/monsters/carrier/gibs/thigh.md2");

	VectorSet (self->mins, -56, -56, -44);
	VectorSet (self->maxs, 56, 56, 44);

	// 2000 - 4000 health
	self->health = 2000 + 1000 * (skill->value - 1);
	if (self->health < 2000)
		self->health = 2000;
	// add health in coop (500 * skill)
	if (coop->value)
		self->health += 500 * skill->value;

	self->gib_health = -200;
	self->mass = 1000;

	self->yaw_speed = 15;
	orig_yaw_speed = self->yaw_speed;

	self->flags |= FL_IMMUNE_LASER;
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	self->pain = carrier_pain;
	self->die = carrier_die;

	self->monsterinfo.melee = NULL;
	self->monsterinfo.stand = carrier_stand;
	self->monsterinfo.walk = carrier_walk;
	self->monsterinfo.run = carrier_run;
	self->monsterinfo.attack = carrier_attack;
	self->monsterinfo.sight = carrier_sight;
	self->monsterinfo.checkattack = Carrier_CheckAttack;
	self->monsterinfo.setskin = carrier_setskin;
	gi.linkentity (self);

	self->monsterinfo.currentmove = &carrier_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	CarrierPrecache ();

	flymonster_start (self);

	self->monsterinfo.attack_finished = 0;

	// the map's reinforcements, or three flyers and a kamikaze
	M_SetupReinforcements (self, "monster_flyer 1;monster_flyer 1;monster_flyer 1;monster_kamikaze 1", 3);

	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	self->monsterinfo.fly_acceleration = 5;
	self->monsterinfo.fly_speed = 50;
	self->monsterinfo.fly_above = true;
	self->monsterinfo.fly_min_distance = 1000;
	self->monsterinfo.fly_max_distance = 1000;
}
