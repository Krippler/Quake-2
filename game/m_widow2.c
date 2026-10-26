/*
==============================================================================

Ground Zero's black widow's second form (monster_widow2), from the
re-release's rerelease/rogue/m_rogue_widow2.cpp: it sweeps its heat
beam, fires its disruptor, pulls you into its jaws with its tongue,
summons stalkers, and blows apart in pieces of its own; with the legs
its first form leaves behind (widowlegs). This game has no other of it.

==============================================================================
*/

#include "g_local.h"
#include "m_widow2.h"

qboolean infront (edict_t *self, edict_t *other);
void WidowCalcSlots (edict_t *self);
void WidowPowerups (edict_t *self);
void Widow_SpawnStalkers (edict_t *self, vec3_t *points);
void Widow_ReadyStalkers (edict_t *self, vec3_t *points);
void gib_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);
void ClipGibVelocity (edict_t *ent);

static int	sound_pain1;
static int	sound_pain2;
static int	sound_pain3;
static int	sound_death;
static int	sound_search1;
static int	sound_tentacles_retract;

static vec3_t spawnpoints[] = {
	{ 30, 135, 0 },
	{ 30, -135, 0 }
};

static float sweep_angles[] = {
	-40.0, -32.0, -24.0, -16.0, -8.0, 0.0, 8.0, 16.0, 24.0, 32.0, 40.0
};

// these offsets used by the tongue
static vec3_t offsets[] = {
	{ 17.48f, 0.10f, 68.92f },
	{ 17.47f, 0.29f, 68.91f },
	{ 17.45f, 0.53f, 68.87f },
	{ 17.42f, 0.78f, 68.81f },
	{ 17.39f, 1.02f, 68.75f },
	{ 17.37f, 1.20f, 68.70f },
	{ 17.36f, 1.24f, 68.71f },
	{ 17.37f, 1.21f, 68.72f },
};

static void widow2_run (edict_t *self);
static void widow2_attack (edict_t *self);
static void widow2_attack_beam (edict_t *self);
static void widow2_reattack_beam (edict_t *self);
static void widow2_spawn_check (edict_t *self);
static void widow2_ready_spawn (edict_t *self);
static void widow2_step (edict_t *self);
static void widow2_spawn_start (edict_t *self);
static void widow2_disrupt_reattack (edict_t *self);
static void widow2_start_searching (edict_t *self);
static void widow2_keep_searching (edict_t *self);
static void widow2_finaldeath (edict_t *self);
static void Widow2Beam (edict_t *self);
static void Widow2SaveBeamTarget (edict_t *self);
static void Widow2SaveDisruptLoc (edict_t *self);
static void WidowDisrupt (edict_t *self);
static void Widow2Tongue (edict_t *self);
static void Widow2TonguePull (edict_t *self);
static void Widow2Crunch (edict_t *self);
static void Widow2Toss (edict_t *self);
static void WidowExplode (edict_t *self);
static void WidowExplosion1 (edict_t *self);
static void WidowExplosion2 (edict_t *self);
static void WidowExplosion3 (edict_t *self);
static void WidowExplosion4 (edict_t *self);
static void WidowExplosion5 (edict_t *self);
static void WidowExplosion6 (edict_t *self);
static void WidowExplosion7 (edict_t *self);
static void WidowExplosionLeg (edict_t *self);
static void ThrowArm1 (edict_t *self);
static void ThrowArm2 (edict_t *self);
static void ThrowWidowGibReal (edict_t *self, char *gibname, int damage, int type, vec3_t startpos, qboolean sized, int hitsound, qboolean fade);

static mframe_t widow2_frames_stand[] = {
	{ ai_stand }
};

static mframe_t widow2_frames_walk[] = {
	{ ai_walk, 9.01f, widow2_step },
	{ ai_walk, 7.55f },
	{ ai_walk, 7.01f },
	{ ai_walk, 6.66f },
	{ ai_walk, 6.20f },
	{ ai_walk, 5.78f, widow2_step },
	{ ai_walk, 7.25f },
	{ ai_walk, 8.37f },
	{ ai_walk, 10.41f }
};

static mframe_t widow2_frames_run[] = {
	{ ai_run, 9.01f, widow2_step },
	{ ai_run, 7.55f },
	{ ai_run, 7.01f },
	{ ai_run, 6.66f },
	{ ai_run, 6.20f },
	{ ai_run, 5.78f, widow2_step },
	{ ai_run, 7.25f },
	{ ai_run, 8.37f },
	{ ai_run, 10.41f }
};

static mframe_t widow2_frames_attack_pre_beam[] = {
	{ ai_charge, 4 },
	{ ai_charge, 4, widow2_step },
	{ ai_charge, 4 },
	{ ai_charge, 4, widow2_attack_beam }
};

static mframe_t widow2_frames_attack_beam[] = {
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, widow2_reattack_beam }
};

static mframe_t widow2_frames_attack_post_beam[] = {
	{ ai_charge, 4 },
	{ ai_charge, 4 }
};

static mframe_t widow2_frames_attack_disrupt[] = {
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2, Widow2SaveDisruptLoc },
	{ ai_charge, -20, WidowDisrupt },
	{ ai_charge, 2 },
	{ ai_charge, 2 },
	{ ai_charge, 2, widow2_disrupt_reattack }
};

static mframe_t widow2_frames_spawn[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow2_spawn_start },
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, Widow2Beam }, // 5
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, widow2_ready_spawn }, // 10
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, Widow2Beam },
	{ ai_charge, 0, widow2_spawn_check },
	{ ai_charge }, // 15
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, widow2_reattack_beam }
};

static mframe_t widow2_frames_tongs[] = {
	{ ai_charge, 0, Widow2Tongue },
	{ ai_charge, 0, Widow2Tongue },
	{ ai_charge, 0, Widow2Tongue },
	{ ai_charge, 0, Widow2TonguePull },
	{ ai_charge, 0, Widow2TonguePull }, // 5
	{ ai_charge, 0, Widow2TonguePull },
	{ ai_charge, 0, Widow2Crunch },
	{ ai_charge, 0, Widow2Toss }
};

static mframe_t widow2_frames_pain[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};

static mframe_t widow2_frames_death[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, WidowExplosion1 }, // 3 boom
	{ ai_move },
	{ ai_move }, // 5

	{ ai_move, 0, WidowExplosion2 }, // 6 boom
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 10

	{ ai_move },
	{ ai_move }, // 12
	{ ai_move },
	{ ai_move },
	{ ai_move }, // 15

	{ ai_move },
	{ ai_move },
	{ ai_move, 0, WidowExplosion3 }, // 18
	{ ai_move },					 // 19
	{ ai_move },					 // 20

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, WidowExplosion4 }, // 25

	{ ai_move }, // 26
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, WidowExplosion5 },
	{ ai_move, 0, WidowExplosionLeg }, // 30

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, WidowExplosion6 },
	{ ai_move }, // 35

	{ ai_move },
	{ ai_move },
	{ ai_move, 0, WidowExplosion7 },
	{ ai_move },
	{ ai_move }, // 40

	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, WidowExplode } // 44
};

static mframe_t widow2_frames_dead[] = {
	{ ai_move, 0, widow2_start_searching },
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
	{ ai_move, 0, widow2_keep_searching }
};

static mframe_t widow2_frames_really_dead[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },

	{ ai_move },
	{ ai_move, 0, widow2_finaldeath }
};

static mmove_t widow2_move_stand = { FRAME_blackwidow3, FRAME_blackwidow3, widow2_frames_stand, NULL };
static mmove_t widow2_move_walk = { FRAME_walk01, FRAME_walk09, widow2_frames_walk, NULL };
static mmove_t widow2_move_run = { FRAME_walk01, FRAME_walk09, widow2_frames_run, NULL };
static mmove_t widow2_move_attack_pre_beam = { FRAME_fireb01, FRAME_fireb04, widow2_frames_attack_pre_beam, NULL };
static mmove_t widow2_move_attack_beam = { FRAME_fireb05, FRAME_fireb09, widow2_frames_attack_beam, NULL };
static mmove_t widow2_move_attack_post_beam = { FRAME_fireb06, FRAME_fireb07, widow2_frames_attack_post_beam, widow2_run };
static mmove_t widow2_move_attack_disrupt = { FRAME_firea01, FRAME_firea07, widow2_frames_attack_disrupt, widow2_run };
static mmove_t widow2_move_spawn = { FRAME_spawn01, FRAME_spawn18, widow2_frames_spawn, NULL };
static mmove_t widow2_move_tongs = { FRAME_tongs01, FRAME_tongs08, widow2_frames_tongs, widow2_run };
static mmove_t widow2_move_pain = { FRAME_pain01, FRAME_pain05, widow2_frames_pain, widow2_run };
static mmove_t widow2_move_death = { FRAME_death01, FRAME_death44, widow2_frames_death, NULL };
static mmove_t widow2_move_dead = { FRAME_dthsrh01, FRAME_dthsrh15, widow2_frames_dead, NULL };
static mmove_t widow2_move_really_dead = { FRAME_dthsrh16, FRAME_dthsrh22, widow2_frames_really_dead, NULL };

static void widow2_search (edict_t *self)
{
	if (random () < 0.5)
		gi.sound (self, CHAN_VOICE, sound_search1, 1, ATTN_NONE, 0);
}

static void Widow2Beam (edict_t *self)
{
	vec3_t	forward, right, target;
	vec3_t	start, targ_angles, vec;
	int		flashnum;

	if ((!self->enemy) || (!self->enemy->inuse))
		return;

	AngleVectors (self->s.angles, forward, right, NULL);

	if ((self->s.frame >= FRAME_fireb05) && (self->s.frame <= FRAME_fireb09))
	{
		// regular beam attack
		Widow2SaveBeamTarget (self);
		flashnum = MZ2_WIDOW2_BEAMER_1 + self->s.frame - FRAME_fireb05;
		G_ProjectSource (self->s.origin, monster_flash_offset[flashnum], forward, right, start);
		VectorCopy (self->pos2, target);
		target[2] += self->enemy->viewheight - 10;
		VectorSubtract (target, start, forward);
		VectorNormalize (forward);
		monster_fire_heatbeam (self, start, forward, vec3_origin, 10, 50, flashnum);
	}
	else if ((self->s.frame >= FRAME_spawn04) && (self->s.frame <= FRAME_spawn14))
	{
		// sweep
		flashnum = MZ2_WIDOW2_BEAM_SWEEP_1 + self->s.frame - FRAME_spawn04;
		G_ProjectSource (self->s.origin, monster_flash_offset[flashnum], forward, right, start);
		VectorSubtract (self->enemy->s.origin, start, target);
		vectoangles2 (target, targ_angles);

		VectorCopy (self->s.angles, vec);

		vec[PITCH] += targ_angles[PITCH];
		vec[YAW] -= sweep_angles[flashnum - MZ2_WIDOW2_BEAM_SWEEP_1];

		AngleVectors (vec, forward, NULL, NULL);
		monster_fire_heatbeam (self, start, forward, vec3_origin, 10, 50, flashnum);
	}
	else
	{
		Widow2SaveBeamTarget (self);
		G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_WIDOW2_BEAMER_1], forward, right, start);

		VectorCopy (self->pos2, target);
		target[2] += self->enemy->viewheight - 10;

		VectorSubtract (target, start, forward);
		VectorNormalize (forward);

		monster_fire_heatbeam (self, start, forward, vec3_origin, 10, 50, MZ2_WIDOW2_BEAM_SWEEP_1);
	}
}

static void widow2_spawn_check (edict_t *self)
{
	Widow2Beam (self);
	Widow_SpawnStalkers (self, spawnpoints);
}

static void widow2_ready_spawn (edict_t *self)
{
	Widow2Beam (self);
	Widow_ReadyStalkers (self, spawnpoints);
}

// its spawn sequence's start: it steers itself, stepping
static void widow2_spawn_start (edict_t *self)
{
	self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
	widow2_step (self);
}

static void widow2_step (edict_t *self)
{
	gi.sound (self, CHAN_BODY, gi.soundindex ("widow/bwstep1.wav"), 1, ATTN_NORM, 0);
}

static void WidowDisrupt (edict_t *self)
{
	vec3_t	start;
	vec3_t	dir;
	vec3_t	forward, right;
	float	len;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_WIDOW_DISRUPTOR], forward, right, start);

	VectorSubtract (self->pos1, self->enemy->s.origin, dir);
	len = VectorLength (dir);

	if (len < 30)
	{
		// calc direction to where we targeted
		VectorSubtract (self->pos1, start, dir);
		VectorNormalize (dir);

		monster_fire_tracker (self, start, dir, 20, 500, self->enemy, MZ2_WIDOW_DISRUPTOR);
	}
	else
	{
		PredictAim (self, self->enemy, start, 1200, true, 0, dir, NULL);
		monster_fire_tracker (self, start, dir, 20, 1200, NULL, MZ2_WIDOW_DISRUPTOR);
	}

	widow2_step (self);
}

static void Widow2SaveDisruptLoc (edict_t *self)
{
	if (self->enemy && self->enemy->inuse)
	{
		VectorCopy (self->enemy->s.origin, self->pos1); // save for aiming the shot
		self->pos1[2] += self->enemy->viewheight;
	}
	else
		VectorClear (self->pos1);
}

static void widow2_disrupt_reattack (edict_t *self)
{
	float luck = random ();

	if (luck < (0.25 + (skill->value * 0.15)))
		self->monsterinfo.nextframe = FRAME_firea01;
}

static void Widow2SaveBeamTarget (edict_t *self)
{
	if (self->enemy && self->enemy->inuse)
	{
		VectorCopy (self->pos1, self->pos2);
		VectorCopy (self->enemy->s.origin, self->pos1); // save for aiming the shot
	}
	else
	{
		VectorClear (self->pos1);
		VectorClear (self->pos2);
	}
}

// whether its tongue can reach from start to end, near enough and not too steep
static qboolean widow2_tongue_attack_ok (vec3_t start, vec3_t end, float range)
{
	vec3_t dir, angles;

	// check for max distance
	VectorSubtract (start, end, dir);
	if (VectorLength (dir) > range)
		return false;

	// check for min/max pitch
	vectoangles2 (dir, angles);
	if (angles[0] < -180)
		angles[0] += 360;
	if (fabs (angles[0]) > 30)
		return false;

	return true;
}

static void Widow2Tongue (edict_t *self)
{
	vec3_t	f, r, u;
	vec3_t	start, end, dir;
	trace_t	tr;

	AngleVectors (self->s.angles, f, r, u);
	G_ProjectSource2 (self->s.origin, offsets[self->s.frame - FRAME_tongs01], f, r, u, start);
	VectorCopy (self->enemy->s.origin, end);
	if (!widow2_tongue_attack_ok (start, end, 256))
	{
		end[2] = self->enemy->s.origin[2] + self->enemy->maxs[2] - 8;
		if (!widow2_tongue_attack_ok (start, end, 256))
		{
			end[2] = self->enemy->s.origin[2] + self->enemy->mins[2] + 8;
			if (!widow2_tongue_attack_ok (start, end, 256))
				return;
		}
	}
	VectorCopy (self->enemy->s.origin, end);

	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);
	if (tr.ent != self->enemy)
		return;

	gi.sound (self, CHAN_WEAPON, sound_tentacles_retract, 1, ATTN_NORM, 0);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_PARASITE_ATTACK);
	gi.WriteShort (self - g_edicts);
	gi.WritePosition (start);
	gi.WritePosition (end);
	gi.multicast (self->s.origin, MULTICAST_PVS);

	VectorSubtract (start, end, dir);
	T_Damage (self->enemy, self, self, dir, self->enemy->s.origin, vec3_origin, 2, 0, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN);
}

static void Widow2TonguePull (edict_t *self)
{
	vec3_t	vec;
	vec3_t	f, r, u;
	vec3_t	start, end;

	if ((!self->enemy) || (!self->enemy->inuse))
	{
		self->monsterinfo.run (self);
		return;
	}

	AngleVectors (self->s.angles, f, r, u);
	G_ProjectSource2 (self->s.origin, offsets[self->s.frame - FRAME_tongs01], f, r, u, start);
	VectorCopy (self->enemy->s.origin, end);

	if (!widow2_tongue_attack_ok (start, end, 256))
		return;

	if (self->enemy->groundentity)
	{
		self->enemy->s.origin[2] += 1;
		self->enemy->groundentity = NULL;
		// interesting, you don't have to relink the player
	}

	VectorSubtract (self->s.origin, self->enemy->s.origin, vec);

	if (self->enemy->client)
	{
		VectorNormalize (vec);
		VectorMA (self->enemy->velocity, 1000, vec, self->enemy->velocity);
	}
	else
	{
		self->enemy->ideal_yaw = vectoyaw2 (vec);
		M_ChangeYaw (self->enemy);
		VectorScale (f, 1000, self->enemy->velocity);
	}
}

static void Widow2Crunch (edict_t *self)
{
	vec3_t aim;

	if ((!self->enemy) || (!self->enemy->inuse))
	{
		self->monsterinfo.run (self);
		return;
	}

	Widow2TonguePull (self);

	// 70 + 32
	VectorSet (aim, 150, 0, 4);
	if (self->s.frame != FRAME_tongs07)
		fire_hit_rr (self, aim, 20 + (rand () % 6), 0);
	else if (self->enemy->groundentity)
		fire_hit_rr (self, aim, 20 + (rand () % 6), 500);
	else // not as much kick if they're in the air .. makes it harder to land on her head
		fire_hit_rr (self, aim, 20 + (rand () % 6), 250);
}

static void Widow2Toss (edict_t *self)
{
	self->timestamp = level.time + 3;
}

static void widow2_start_searching (edict_t *self)
{
	self->count = 0;
}

static void widow2_keep_searching (edict_t *self)
{
	if (self->count <= 2)
	{
		self->monsterinfo.currentmove = &widow2_move_dead;
		self->s.frame = FRAME_dthsrh01;
		self->count++;
		return;
	}

	self->monsterinfo.currentmove = &widow2_move_really_dead;
}

static void widow2_finaldeath (edict_t *self)
{
	VectorSet (self->mins, -70, -70, 0);
	VectorSet (self->maxs, 70, 70, 80);
	self->movetype = MOVETYPE_TOSS;
	self->takedamage = DAMAGE_YES;
	self->nextthink = 0;
	gi.linkentity (self);
}

static void widow2_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &widow2_move_stand;
}

static void widow2_run (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &widow2_move_stand;
	else
		self->monsterinfo.currentmove = &widow2_move_run;
}

static void widow2_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &widow2_move_walk;
}

static void widow2_melee (edict_t *self)
{
	if (self->timestamp >= level.time)
		widow2_attack (self);
	else
		self->monsterinfo.currentmove = &widow2_move_tongs;
}

static void widow2_attack (edict_t *self)
{
	float		luck, real_enemy_range;
	qboolean	blocked = false;

	if (self->monsterinfo.aiflags & AI_BLOCKED)
	{
		blocked = true;
		self->monsterinfo.aiflags &= ~AI_BLOCKED;
	}

	if (!self->enemy)
		return;

	real_enemy_range = realrange (self, self->enemy);

	// melee attack
	if (self->timestamp < level.time)
	{
		if (real_enemy_range < 300)
		{
			vec3_t f, r, u, spot1, spot2;

			AngleVectors (self->s.angles, f, r, u);
			G_ProjectSource2 (self->s.origin, offsets[0], f, r, u, spot1);
			VectorCopy (self->enemy->s.origin, spot2);
			if (widow2_tongue_attack_ok (spot1, spot2, 256))
			{
				// melee attack ok

				// be nice in easy mode
				if (skill->value != 0 || (rand () & 3))
				{
					self->monsterinfo.currentmove = &widow2_move_tongs;
					return;
				}
			}
		}
	}

	if (self->bad_area)
	{
		if ((random () < 0.75) || (level.time < self->monsterinfo.attack_finished))
			self->monsterinfo.currentmove = &widow2_move_attack_pre_beam;
		else
			self->monsterinfo.currentmove = &widow2_move_attack_disrupt;
		return;
	}

	WidowCalcSlots (self);

	// if we can't see the target, spawn stuff
	if ((self->monsterinfo.attack_state == AS_BLIND) && (M_SlotsLeft (self) >= 2))
	{
		self->monsterinfo.currentmove = &widow2_move_spawn;
		return;
	}

	// accept bias towards spawning
	if (blocked && (M_SlotsLeft (self) >= 2))
	{
		self->monsterinfo.currentmove = &widow2_move_spawn;
		return;
	}

	if (real_enemy_range < 600)
	{
		luck = random ();
		if (M_SlotsLeft (self) >= 2)
		{
			if (luck <= 0.40)
				self->monsterinfo.currentmove = &widow2_move_attack_pre_beam;
			else if ((luck <= 0.7) && !(level.time < self->monsterinfo.attack_finished))
				self->monsterinfo.currentmove = &widow2_move_attack_disrupt;
			else
				self->monsterinfo.currentmove = &widow2_move_spawn;
		}
		else
		{
			if ((luck <= 0.50) || (level.time < self->monsterinfo.attack_finished))
				self->monsterinfo.currentmove = &widow2_move_attack_pre_beam;
			else
				self->monsterinfo.currentmove = &widow2_move_attack_disrupt;
		}
	}
	else
	{
		luck = random ();
		if (M_SlotsLeft (self) >= 2)
		{
			if (luck < 0.3)
				self->monsterinfo.currentmove = &widow2_move_attack_pre_beam;
			else if ((luck < 0.65) || (level.time < self->monsterinfo.attack_finished))
				self->monsterinfo.currentmove = &widow2_move_spawn;
			else
				self->monsterinfo.currentmove = &widow2_move_attack_disrupt;
		}
		else
		{
			if ((luck < 0.45) || (level.time < self->monsterinfo.attack_finished))
				self->monsterinfo.currentmove = &widow2_move_attack_pre_beam;
			else
				self->monsterinfo.currentmove = &widow2_move_attack_disrupt;
		}
	}
}

static void widow2_attack_beam (edict_t *self)
{
	self->monsterinfo.currentmove = &widow2_move_attack_beam;
	widow2_step (self);
}

static void widow2_reattack_beam (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

	if (infront (self, self->enemy))
	{
		if (random () <= 0.5)
		{
			if ((random () < 0.7) || (M_SlotsLeft (self) < 2))
				self->monsterinfo.currentmove = &widow2_move_attack_beam;
			else
				self->monsterinfo.currentmove = &widow2_move_spawn;
		}
		else
			self->monsterinfo.currentmove = &widow2_move_attack_post_beam;
	}
	else
		self->monsterinfo.currentmove = &widow2_move_attack_post_beam;
}

static void widow2_pain (edict_t *self, edict_t *other, float kick, int damage)
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

	if (damage >= 15)
	{
		if (damage < 75)
		{
			if ((skill->value < 3) && (random () < (0.6 - (0.2 * skill->value))))
			{
				self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
				self->monsterinfo.currentmove = &widow2_move_pain;
			}
		}
		else
		{
			if ((skill->value < 3) && (random () < (0.75 - (0.1 * skill->value))))
			{
				self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
				self->monsterinfo.currentmove = &widow2_move_pain;
			}
		}
	}
}

static void widow2_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

// its stalkers die with it
static void KillChildren (edict_t *self)
{
	edict_t *ent = NULL;

	while (1)
	{
		ent = G_Find (ent, FOFS(classname), "monster_stalker");
		if (!ent)
			return;

		// FIXME - may need to stagger
		if ((ent->inuse) && (ent->health > 0))
			T_Damage (ent, self, self, vec3_origin, ent->s.origin, vec3_origin, (ent->health + 1), 0, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN);
	}
}

static void widow2_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	static gib_def_t	gibs[] = {
		{ 1, "models/objects/gibs/chest/tris.md2", GIB_ORGANIC },
		{ 1, "models/objects/gibs/head2/tris.md2", GIB_ORGANIC | GIB_HEAD },
		{ 0, NULL }
	};
	int		n;
	int		clipped;

	// check for gib
	if (self->deadflag && M_CheckGib (self))
	{
		clipped = damage < 100 ? damage : 100;

		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		for (n = 0; n < 2; n++)
			ThrowWidowGibReal (self, "models/objects/gibs/bone/tris.md2", clipped, GIB_ORGANIC, NULL, false, 0, false);
		for (n = 0; n < 3; n++)
			ThrowWidowGibReal (self, "models/objects/gibs/sm_meat/tris.md2", clipped, GIB_ORGANIC, NULL, false, 0, false);
		for (n = 0; n < 3; n++)
		{
			ThrowWidowGibReal (self, "models/monsters/blackwidow2/gib1/tris.md2", clipped, GIB_METALLIC, NULL, true, 0, false);
			ThrowWidowGibReal (self, "models/monsters/blackwidow2/gib2/tris.md2", clipped, GIB_METALLIC, NULL, true, gi.soundindex ("misc/fhit3.wav"), false);
		}
		for (n = 0; n < 2; n++)
		{
			ThrowWidowGibReal (self, "models/monsters/blackwidow2/gib3/tris.md2", clipped, GIB_METALLIC, NULL, true, 0, false);
			ThrowWidowGibReal (self, "models/monsters/blackwidow/gib3/tris.md2", clipped, GIB_METALLIC, NULL, true, 0, false);
		}
		ThrowGibs (self, damage, gibs);

		return;
	}

	if (self->deadflag)
		return;

	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NONE, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_NO;
	self->count = 0;
	KillChildren (self);
	self->monsterinfo.quad_time = 0;
	self->monsterinfo.double_time = 0;
	self->monsterinfo.invincible_time = 0;
	self->monsterinfo.currentmove = &widow2_move_death;
}

static qboolean Widow2_CheckAttack (edict_t *self)
{
	if (!self->enemy)
		return false;

	WidowPowerups (self);

	if ((random () < 0.8) && (M_SlotsLeft (self) >= 2) && (realrange (self, self->enemy) > 150))
	{
		self->monsterinfo.aiflags |= AI_BLOCKED;
		self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	return M_CheckAttack_Base (self, 0.4, 0.8, 0.8, 0.5, 0, 0);
}

static void Widow2Precache (void)
{
	// cache in all of the stalker stuff, widow stuff, spawngro stuff, gibs
	gi.soundindex ("parasite/parpain1.wav");
	gi.soundindex ("parasite/parpain2.wav");
	gi.soundindex ("parasite/pardeth1.wav");
	gi.soundindex ("parasite/paratck1.wav");
	gi.soundindex ("parasite/parsght1.wav");
	gi.soundindex ("infantry/melee2.wav");
	gi.soundindex ("misc/fhit3.wav");

	gi.soundindex ("tank/tnkatck3.wav");
	gi.soundindex ("weapons/disrupt.wav");
	gi.soundindex ("weapons/disint2.wav");

	gi.modelindex ("models/monsters/stalker/tris.md2");
	gi.modelindex ("models/items/spawngro3/tris.md2");
	gi.modelindex ("models/objects/gibs/sm_metal/tris.md2");
	gi.modelindex ("models/objects/laser/tris.md2");
	gi.modelindex ("models/proj/disintegrator/tris.md2");

	gi.modelindex ("models/monsters/blackwidow/gib1/tris.md2");
	gi.modelindex ("models/monsters/blackwidow/gib2/tris.md2");
	gi.modelindex ("models/monsters/blackwidow/gib3/tris.md2");
	gi.modelindex ("models/monsters/blackwidow/gib4/tris.md2");
	gi.modelindex ("models/monsters/blackwidow2/gib1/tris.md2");
	gi.modelindex ("models/monsters/blackwidow2/gib2/tris.md2");
	gi.modelindex ("models/monsters/blackwidow2/gib3/tris.md2");
	gi.modelindex ("models/monsters/blackwidow2/gib4/tris.md2");
}

/*QUAKED monster_widow2 (1 .5 0) (-70 -70 0) (70 70 144) Ambush Trigger_Spawn Sight
 */
void SP_monster_widow2 (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}

	sound_pain1 = gi.soundindex ("widow/bw2pain1.wav");
	sound_pain2 = gi.soundindex ("widow/bw2pain2.wav");
	sound_pain3 = gi.soundindex ("widow/bw2pain3.wav");
	sound_death = gi.soundindex ("widow/death.wav");
	sound_search1 = gi.soundindex ("bosshovr/bhvunqv1.wav");
	sound_tentacles_retract = gi.soundindex ("brain/brnatck3.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/blackwidow2/tris.md2");
	VectorSet (self->mins, -70, -70, 0);
	VectorSet (self->maxs, 70, 70, 144);

	self->health = 2000 + 800 + 1000 * skill->value;
	if (coop->value)
		self->health += 500 * skill->value;
	self->gib_health = -900;
	self->mass = 2500;

	if (skill->value == 3)
	{
		self->monsterinfo.power_armor_type = POWER_ARMOR_SHIELD;
		self->monsterinfo.power_armor_power = 750;
	}

	self->yaw_speed = 30;

	self->flags |= FL_IMMUNE_LASER;
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	self->pain = widow2_pain;
	self->die = widow2_die;

	self->monsterinfo.melee = widow2_melee;
	self->monsterinfo.stand = widow2_stand;
	self->monsterinfo.walk = widow2_walk;
	self->monsterinfo.run = widow2_run;
	self->monsterinfo.attack = widow2_attack;
	self->monsterinfo.search = widow2_search;
	self->monsterinfo.checkattack = Widow2_CheckAttack;
	self->monsterinfo.setskin = widow2_setskin;
	gi.linkentity (self);

	self->monsterinfo.currentmove = &widow2_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	Widow2Precache ();
	WidowCalcSlots (self);
	walkmonster_start (self);
}

//
// Death sequence stuff
//

static void WidowVelocityForDamage (int damage, vec3_t v)
{
	v[0] = damage * crandom ();
	v[1] = damage * crandom ();
	v[2] = damage * crandom () + 200.0;
}

static void widow_gib_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	self->solid = SOLID_NOT;
	self->touch = NULL;
	self->s.angles[PITCH] = 0;
	self->s.angles[ROLL] = 0;
	VectorClear (self->avelocity);

	if (self->style)
		gi.sound (self, CHAN_VOICE, self->style, 1, ATTN_NORM, 0);
}

// a gib of its own, from about it or where given; big ones bounce and land
static void ThrowWidowGibReal (edict_t *self, char *gibname, int damage, int type, vec3_t startpos, qboolean sized, int hitsound, qboolean fade)
{
	edict_t	*gib;
	vec3_t	vd;
	vec3_t	origin;
	vec3_t	size;
	float	vscale;

	if (!gibname)
		return;

	gib = G_Spawn ();

	if (startpos)
		VectorCopy (startpos, gib->s.origin);
	else
	{
		VectorScale (self->size, 0.5, size);
		VectorAdd (self->absmin, size, origin);
		gib->s.origin[0] = origin[0] + crandom () * size[0];
		gib->s.origin[1] = origin[1] + crandom () * size[1];
		gib->s.origin[2] = origin[2] + crandom () * size[2];
	}

	gib->solid = SOLID_NOT;
	gib->s.effects |= EF_GIB;
	gib->flags |= FL_NO_KNOCKBACK;
	gib->takedamage = DAMAGE_YES;
	gib->die = gib_die;
	gib->classname = "gib";

	gib->think = G_FreeEdict;
	if (fade)
		// sized gibs last longer
		gib->nextthink = level.time + (sized ? 20 + random () * 15 : 5 + random () * 10);
	else
		// sized gibs last longer
		gib->nextthink = level.time + (sized ? 60 + random () * 15 : 25 + random () * 10);

	if (!(type & GIB_METALLIC))
	{
		gib->movetype = MOVETYPE_TOSS;
		vscale = 0.5;
	}
	else
	{
		gib->movetype = MOVETYPE_BOUNCE;
		vscale = 1.0;
	}

	WidowVelocityForDamage (damage, vd);
	VectorMA (self->velocity, vscale, vd, gib->velocity);
	ClipGibVelocity (gib);

	gi.setmodel (gib, gibname);

	if (sized)
	{
		gib->style = hitsound;
		gib->solid = SOLID_BBOX;
		gib->avelocity[0] = random () * 400;
		gib->avelocity[1] = random () * 400;
		gib->avelocity[2] = random () * 400;
		if (gib->velocity[2] < 0)
			gib->velocity[2] *= -1;
		gib->velocity[0] *= 2;
		gib->velocity[1] *= 2;
		ClipGibVelocity (gib);
		if (gib->velocity[2] < 350)
			gib->velocity[2] = 350 + random () * 100;
		gib->gravity = 0.25;
		gib->touch = widow_gib_touch;
		gib->owner = self;
		if (gib->s.modelindex == gi.modelindex ("models/monsters/blackwidow2/gib2/tris.md2"))
		{
			VectorSet (gib->mins, -10, -10, 0);
			VectorSet (gib->maxs, 10, 10, 10);
		}
		else
		{
			VectorSet (gib->mins, -5, -5, 0);
			VectorSet (gib->maxs, 5, 5, 5);
		}
	}
	else
	{
		gib->velocity[0] *= 2;
		gib->velocity[1] *= 2;
		gib->avelocity[0] = random () * 600;
		gib->avelocity[1] = random () * 600;
		gib->avelocity[2] = random () * 600;
	}

	gi.linkentity (gib);
}

static void ThrowWidowGibLoc (edict_t *self, char *gibname, int damage, int type, vec3_t startpos, qboolean fade)
{
	ThrowWidowGibReal (self, gibname, damage, type, startpos, false, 0, fade);
}

static void ThrowWidowGibSized (edict_t *self, char *gibname, int damage, int type, vec3_t startpos, int hitsound, qboolean fade)
{
	ThrowWidowGibReal (self, gibname, damage, type, startpos, true, hitsound, fade);
}

static void ThrowSmallStuff (edict_t *self, vec3_t point)
{
	int n;

	for (n = 0; n < 2; n++)
		ThrowWidowGibLoc (self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_ORGANIC, point, false);
	ThrowWidowGibLoc (self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, point, false);
	ThrowWidowGibLoc (self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, point, false);
}

static void ThrowMoreStuff (edict_t *self, vec3_t point)
{
	int n;

	if (coop->value)
	{
		ThrowSmallStuff (self, point);
		return;
	}

	for (n = 0; n < 1; n++)
		ThrowWidowGibLoc (self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_ORGANIC, point, false);
	for (n = 0; n < 2; n++)
		ThrowWidowGibLoc (self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, point, false);
	for (n = 0; n < 3; n++)
		ThrowWidowGibLoc (self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, point, false);
}

static void WidowExplode (edict_t *self)
{
	vec3_t	org;
	int		n;

	self->think = WidowExplode;

	VectorCopy (self->s.origin, org);
	org[2] += 24 + (rand () % 16);
	if (self->count < 8)
		org[2] += 24 + (rand () % 32);
	switch (self->count)
	{
	case 0:
		org[0] -= 24;
		org[1] -= 24;
		break;
	case 1:
		org[0] += 24;
		org[1] += 24;
		ThrowSmallStuff (self, org);
		break;
	case 2:
		org[0] += 24;
		org[1] -= 24;
		break;
	case 3:
		org[0] -= 24;
		org[1] += 24;
		ThrowMoreStuff (self, org);
		break;
	case 4:
		org[0] -= 48;
		org[1] -= 48;
		break;
	case 5:
		org[0] += 48;
		org[1] += 48;
		ThrowArm1 (self);
		break;
	case 6:
		org[0] -= 48;
		org[1] += 48;
		ThrowArm2 (self);
		break;
	case 7:
		org[0] += 48;
		org[1] -= 48;
		ThrowSmallStuff (self, org);
		break;
	case 8:
		org[0] += 18;
		org[1] += 18;
		org[2] = self->s.origin[2] + 48;
		ThrowMoreStuff (self, org);
		break;
	case 9:
		org[0] -= 18;
		org[1] += 18;
		org[2] = self->s.origin[2] + 48;
		break;
	case 10:
		org[0] += 18;
		org[1] -= 18;
		org[2] = self->s.origin[2] + 48;
		break;
	case 11:
		org[0] -= 18;
		org[1] -= 18;
		org[2] = self->s.origin[2] + 48;
		break;
	case 12:
		self->s.sound = 0;
		for (n = 0; n < 1; n++)
			ThrowWidowGibReal (self, "models/objects/gibs/sm_meat/tris.md2", 400, GIB_ORGANIC, NULL, false, 0, true);
		for (n = 0; n < 2; n++)
			ThrowWidowGibReal (self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, NULL, false, 0, true);
		for (n = 0; n < 2; n++)
			ThrowWidowGibReal (self, "models/objects/gibs/sm_metal/tris.md2", 400, GIB_METALLIC, NULL, false, 0, true);
		self->deadflag = DEAD_DEAD;
		self->think = monster_think;
		self->nextthink = level.time + FRAMETIME;
		self->monsterinfo.currentmove = &widow2_move_dead;
		return;
	}

	self->count++;
	if (self->count >= 9 && self->count <= 12)
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_EXPLOSION1_BIG);
		gi.WritePosition (org);
		gi.multicast (self->s.origin, MULTICAST_ALL);
	}
	else
	{
		gi.WriteByte (svc_temp_entity);
		if (self->count % 2)
			gi.WriteByte (TE_EXPLOSION1);
		else
			gi.WriteByte (TE_EXPLOSION1_NP);
		gi.WritePosition (org);
		gi.multicast (self->s.origin, MULTICAST_ALL);
	}

	self->nextthink = level.time + FRAMETIME;
}

// an explosion so far forward, right and up of it, and its bits
static void WidowExplosionAt (edict_t *self, vec3_t offset)
{
	int		n;
	vec3_t	f, r, u, startpoint;

	AngleVectors (self->s.angles, f, r, u);
	G_ProjectSource2 (self->s.origin, offset, f, r, u, startpoint);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1);
	gi.WritePosition (startpoint);
	gi.multicast (self->s.origin, MULTICAST_ALL);

	for (n = 0; n < 1; n++)
		ThrowWidowGibLoc (self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_ORGANIC, startpoint, false);
	for (n = 0; n < 1; n++)
		ThrowWidowGibLoc (self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
	for (n = 0; n < 2; n++)
		ThrowWidowGibLoc (self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, startpoint, false);
}

static void WidowExplosion1 (edict_t *self)
{
	vec3_t offset = { 23.74f, -37.67f, 76.96f };
	WidowExplosionAt (self, offset);
}

static void WidowExplosion2 (edict_t *self)
{
	vec3_t offset = { -20.49f, 36.92f, 73.52f };
	WidowExplosionAt (self, offset);
}

static void WidowExplosion3 (edict_t *self)
{
	vec3_t offset = { 2.11f, 0.05f, 92.20f };
	WidowExplosionAt (self, offset);
}

static void WidowExplosion4 (edict_t *self)
{
	vec3_t offset = { -28.04f, -35.57f, -77.56f };
	WidowExplosionAt (self, offset);
}

static void WidowExplosion5 (edict_t *self)
{
	vec3_t offset = { -20.11f, -1.11f, 40.76f };
	WidowExplosionAt (self, offset);
}

static void WidowExplosion6 (edict_t *self)
{
	vec3_t offset = { -20.11f, -1.11f, 40.76f };
	WidowExplosionAt (self, offset);
}

static void WidowExplosion7 (edict_t *self)
{
	vec3_t offset = { -20.11f, -1.11f, 40.76f };
	WidowExplosionAt (self, offset);
}

static void WidowExplosionLeg (edict_t *self)
{
	vec3_t f, r, u, startpoint;
	vec3_t offset1 = { -31.89f, -47.86f, 67.02f };
	vec3_t offset2 = { -44.9f, -82.14f, 54.72f };

	AngleVectors (self->s.angles, f, r, u);
	G_ProjectSource2 (self->s.origin, offset1, f, r, u, startpoint);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1_BIG);
	gi.WritePosition (startpoint);
	gi.multicast (self->s.origin, MULTICAST_ALL);

	ThrowWidowGibSized (self, "models/monsters/blackwidow2/gib2/tris.md2", 200, GIB_METALLIC, startpoint, gi.soundindex ("misc/fhit3.wav"), false);
	ThrowWidowGibLoc (self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_ORGANIC, startpoint, false);
	ThrowWidowGibLoc (self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);

	G_ProjectSource2 (self->s.origin, offset2, f, r, u, startpoint);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1);
	gi.WritePosition (startpoint);
	gi.multicast (self->s.origin, MULTICAST_ALL);

	ThrowWidowGibSized (self, "models/monsters/blackwidow2/gib1/tris.md2", 300, GIB_METALLIC, startpoint, gi.soundindex ("misc/fhit3.wav"), false);
	ThrowWidowGibLoc (self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_ORGANIC, startpoint, false);
	ThrowWidowGibLoc (self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
}

static void ThrowArm1 (edict_t *self)
{
	int		n;
	vec3_t	f, r, u, startpoint;
	vec3_t	offset1 = { 65.76f, 17.52f, 7.56f };

	AngleVectors (self->s.angles, f, r, u);
	G_ProjectSource2 (self->s.origin, offset1, f, r, u, startpoint);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1_BIG);
	gi.WritePosition (startpoint);
	gi.multicast (self->s.origin, MULTICAST_ALL);

	for (n = 0; n < 2; n++)
		ThrowWidowGibLoc (self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
}

static void ThrowArm2 (edict_t *self)
{
	vec3_t	f, r, u, startpoint;
	vec3_t	offset1 = { 65.76f, 17.52f, 7.56f };

	AngleVectors (self->s.angles, f, r, u);
	G_ProjectSource2 (self->s.origin, offset1, f, r, u, startpoint);

	ThrowWidowGibSized (self, "models/monsters/blackwidow2/gib4/tris.md2", 200, GIB_METALLIC, startpoint, gi.soundindex ("misc/fhit3.wav"), false);
	ThrowWidowGibLoc (self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_ORGANIC, startpoint, false);
}

//
// the legs its first form leaves (rogue/g_rogue_spawn.cpp's): they burn
// and break apart
//

#define MAX_LEGSFRAME	23
#define LEG_WAIT_TIME	1

static void widowlegs_blast (edict_t *self, vec3_t offset, vec3_t point)
{
	vec3_t f, r, u;

	AngleVectors (self->s.angles, f, r, u);
	G_ProjectSource2 (self->s.origin, offset, f, r, u, point);
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1);
	gi.WritePosition (point);
	gi.multicast (point, MULTICAST_ALL);
}

static void widowlegs_think (edict_t *self)
{
	vec3_t offset;
	vec3_t point;

	if (self->s.frame == 17)
	{
		VectorSet (offset, 11.77f, -7.24f, 23.31f);
		widowlegs_blast (self, offset, point);
		ThrowSmallStuff (self, point);
	}

	if (self->s.frame < MAX_LEGSFRAME)
	{
		self->s.frame++;
		self->nextthink = level.time + FRAMETIME;
		return;
	}
	else if (self->wait == 0)
	{
		self->wait = level.time + LEG_WAIT_TIME;
	}
	if (level.time > self->wait)
	{
		VectorSet (offset, -65.6f, -8.44f, 28.59f);
		widowlegs_blast (self, offset, point);
		ThrowSmallStuff (self, point);

		ThrowWidowGibSized (self, "models/monsters/blackwidow/gib1/tris.md2", 80 + (int) (random () * 20.0), GIB_METALLIC, point, 0, true);
		ThrowWidowGibSized (self, "models/monsters/blackwidow/gib2/tris.md2", 80 + (int) (random () * 20.0), GIB_METALLIC, point, 0, true);

		VectorSet (offset, -1.04f, -51.18f, 7.04f);
		widowlegs_blast (self, offset, point);
		ThrowSmallStuff (self, point);

		ThrowWidowGibSized (self, "models/monsters/blackwidow/gib1/tris.md2", 80 + (int) (random () * 20.0), GIB_METALLIC, point, 0, true);
		ThrowWidowGibSized (self, "models/monsters/blackwidow/gib2/tris.md2", 80 + (int) (random () * 20.0), GIB_METALLIC, point, 0, true);
		ThrowWidowGibSized (self, "models/monsters/blackwidow/gib3/tris.md2", 80 + (int) (random () * 20.0), GIB_METALLIC, point, 0, true);

		G_FreeEdict (self);
		return;
	}
	if ((level.time > self->wait - 0.5) && (self->count == 0))
	{
		self->count = 1;

		VectorSet (offset, 31, -88.7f, 10.96f);
		widowlegs_blast (self, offset, point);

		VectorSet (offset, -12.67f, -4.39f, 15.68f);
		widowlegs_blast (self, offset, point);

		self->nextthink = level.time + FRAMETIME;
		return;
	}
	self->nextthink = level.time + FRAMETIME;
}

void Widowlegs_Spawn (vec3_t startpos, vec3_t angles)
{
	edict_t *ent;

	ent = G_Spawn ();
	VectorCopy (startpos, ent->s.origin);
	VectorCopy (angles, ent->s.angles);
	ent->solid = SOLID_NOT;
	ent->s.renderfx = RF_IR_VISIBLE;
	ent->movetype = MOVETYPE_NONE;
	ent->classname = "widowlegs";

	ent->s.modelindex = gi.modelindex ("models/monsters/legs/tris.md2");
	ent->think = widowlegs_think;

	ent->nextthink = level.time + FRAMETIME;
	gi.linkentity (ent);
}
