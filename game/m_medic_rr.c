// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

MEDIC, the 2023 re-release's

Its m_medic.cpp, in C, for its own maps (RR_Map): the medic and Ground
Zero's medic commander. It heals the strongest corpse it can see within
reach, keeps to it while not badly hurt, gives up on one its cable can't
reach (and won't try that one again), and gibs it if it would rise inside
something; the risen keep their health and stink, and lose their flies.
It fires a hyperblaster burst after its blaster shots, ducks and
sidesteps, and gibs into pieces of its own. The commander summons its
reinforcements (m_medic.c's, which are the re-release's already). Its
frames are 1997's. m_medic.c is 1997's medic, for every other map.

What it does at 40 Hz is done at this game's 10.
==============================================================================
*/

#include "g_local.h"
#include "m_medic.h"

qboolean blocked_checkplat (edict_t *self, float dist);
qboolean visible (edict_t *self, edict_t *other);
qboolean FindTarget (edict_t *self);
void FoundTarget (edict_t *self);
void HuntTarget (edict_t *self);
void ED_CallSpawn (edict_t *ent);
void M_SetEffects (edict_t *ent);
qboolean M_CorpseAtRest (edict_t *ent);

// the commander's summoning, m_medic.c's
void medic_determine_spawn (edict_t *self);
void medic_spawngrows (edict_t *self);
void medic_finish_spawn (edict_t *self);

#define MEDIC_MIN_DISTANCE		32
#define MEDIC_MAX_HEAL_DISTANCE	400
#define MEDIC_TRY_TIME			10

static int	sound_idle1;
static int	sound_pain1;
static int	sound_pain2;
static int	sound_die;
static int	sound_sight;
static int	sound_search;
static int	sound_hook_launch;
static int	sound_hook_hit;
static int	sound_hook_heal;
static int	sound_hook_retract;

// PMM - commander sounds
static int	commander_sound_idle1;
static int	commander_sound_pain1;
static int	commander_sound_pain2;
static int	commander_sound_die;
static int	commander_sound_sight;
static int	commander_sound_search;
static int	commander_sound_hook_launch;
static int	commander_sound_hook_hit;
static int	commander_sound_hook_heal;
static int	commander_sound_hook_retract;
static int	commander_sound_spawn;

static char *default_reinforcements = "monster_soldier_light 1;monster_soldier 2;monster_soldier_ss 2;monster_infantry 3;monster_gunner 4;monster_medic 5;monster_gladiator 6";
#define DEFAULT_MONSTER_SLOTS_BASE	3

static void medic_run (edict_t *self);
static void medic_stand (edict_t *self);
static void medic_dead (edict_t *self);
static void medic_shrink (edict_t *self);
static void medic_attack (edict_t *self);

static void cleanupHeal (edict_t *self, qboolean change_frame)
{
	// clean up target, if we have one and it's legit
	if (self->enemy && self->enemy->inuse)
		cleanupHealTarget (self->enemy);

	if (self->oldenemy && self->oldenemy->inuse && self->oldenemy->health > 0)
	{
		self->enemy = self->oldenemy;
		HuntTarget (self);
	}
	else
	{
		self->enemy = self->goalentity = NULL;
		self->oldenemy = NULL;
		if (!FindTarget (self))
		{
			// no valid enemy, so stop acting
			self->monsterinfo.pausetime = 100000000;
			self->monsterinfo.stand (self);
			return;
		}
	}

	if (change_frame)
		self->monsterinfo.nextframe = FRAME_attack52;
}

void abortHeal (edict_t *self, qboolean change_frame, qboolean gib, qboolean mark)
{
	int		hurt;
	vec3_t	pain_normal = { 0, 0, 1 };

	if (self->enemy && self->enemy->inuse)
	{
		cleanupHealTarget (self->enemy);

		// gib em!
		if (mark)
		{
			// if the first badMedic slot is filled by a medic, skip it and use the second one
			if ((self->enemy->monsterinfo.badMedic1) && (self->enemy->monsterinfo.badMedic1->inuse) && (!strncmp (self->enemy->monsterinfo.badMedic1->classname, "monster_medic", 13)))
			{
				self->enemy->monsterinfo.badMedic2 = self;
			}
			else
			{
				self->enemy->monsterinfo.badMedic1 = self;
			}
		}

		if (gib)
		{
			if (self->enemy->gib_health)
				hurt = -self->enemy->gib_health;
			else
				hurt = 500;

			T_Damage (self->enemy, self, self, vec3_origin, self->enemy->s.origin,
					 pain_normal, hurt, 0, 0, MOD_UNKNOWN);
		}
	}

	// clean up self
	// clean up target
	cleanupHeal (self, change_frame);

	self->monsterinfo.aiflags &= ~AI_MEDIC;
	self->monsterinfo.medicTries = 0;
}

static edict_t *medic_FindDeadMonster (edict_t *self)
{
	float	radius;
	edict_t	*ent = NULL;
	edict_t	*best = NULL;

	if (self->monsterinfo.react_to_damage_time > level.time)
		return NULL;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		radius = MEDIC_MAX_HEAL_DISTANCE;
	else
		radius = 1024;

	while ((ent = findradius (ent, self->s.origin, radius)) != NULL)
	{
		if (ent == self)
			continue;
		if (!(ent->svflags & SVF_MONSTER))
			continue;
		if (ent->monsterinfo.aiflags & AI_GOOD_GUY)
			continue;
		// check to make sure we haven't bailed on this guy already
		if ((ent->monsterinfo.badMedic1 == self) || (ent->monsterinfo.badMedic2 == self))
			continue;
		if (ent->monsterinfo.healer)
			// FIXME - this is correcting a bug that is somewhere else
			// if the healer is a monster, and it's in medic mode .. continue .. otherwise
			//   we will override the healer, if it passes all the other tests
			if ((ent->monsterinfo.healer->inuse) && (ent->monsterinfo.healer->health > 0) &&
				(ent->monsterinfo.healer->svflags & SVF_MONSTER) && (ent->monsterinfo.healer->monsterinfo.aiflags & AI_MEDIC))
				continue;
		if (ent->health > 0)
			continue;
		if (!M_CorpseAtRest (ent))
			continue;
		if (!visible (self, ent))
			continue;
		// FIXME - there's got to be a better way ..
		// make sure we don't spawn people right on top of us
		if (realrange (self, ent) <= MEDIC_MIN_DISTANCE)
			continue;
		if (!best)
		{
			best = ent;
			continue;
		}
		if (ent->max_health <= best->max_health)
			continue;
		best = ent;
	}

	if (best)
		self->timestamp = level.time + MEDIC_TRY_TIME;

	return best;
}

// heal this one
static qboolean medic_take_patient (edict_t *self)
{
	edict_t	*ent;

	ent = medic_FindDeadMonster (self);
	if (!ent)
		return false;

	self->oldenemy = self->enemy;
	self->enemy = ent;
	self->enemy->monsterinfo.healer = self;
	self->monsterinfo.aiflags |= AI_MEDIC;
	FoundTarget (self);
	return true;
}

static void medic_idle (edict_t *self)
{
	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound (self, CHAN_VOICE, sound_idle1, 1, ATTN_IDLE, 0);
	else
		gi.sound (self, CHAN_VOICE, commander_sound_idle1, 1, ATTN_IDLE, 0);

	if (!self->oldenemy)
		medic_take_patient (self);
}

static void medic_search (edict_t *self)
{
	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_IDLE, 0);
	else
		gi.sound (self, CHAN_VOICE, commander_sound_search, 1, ATTN_IDLE, 0);

	if (!self->oldenemy)
		medic_take_patient (self);
}

static void medic_sight (edict_t *self, edict_t *other)
{
	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, commander_sound_sight, 1, ATTN_NORM, 0);
}

static mframe_t medic_frames_stand[] = {
	{ ai_stand, 0, medic_idle },
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
};
static mmove_t medic_move_stand = { FRAME_wait1, FRAME_wait90, medic_frames_stand, NULL };

static void medic_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &medic_move_stand;
}

static mframe_t medic_frames_walk[] = {
	{ ai_walk, 6.2f },
	{ ai_walk, 18.1f, monster_footstep },
	{ ai_walk, 1 },
	{ ai_walk, 9 },
	{ ai_walk, 10 },
	{ ai_walk, 9 },
	{ ai_walk, 11 },
	{ ai_walk, 11.6f, monster_footstep },
	{ ai_walk, 2 },
	{ ai_walk, 9.9f },
	{ ai_walk, 14 },
	{ ai_walk, 9.3f }
};
static mmove_t medic_move_walk = { FRAME_walk1, FRAME_walk12, medic_frames_walk, NULL };

static void medic_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &medic_move_walk;
}

static mframe_t medic_frames_run[] = {
	{ ai_run, 18 },
	{ ai_run, 22.5f, monster_footstep },
	{ ai_run, 25.4f, monster_done_dodge },
	{ ai_run, 23.4f, monster_footstep },
	{ ai_run, 24 },
	{ ai_run, 35.6f }
};
static mmove_t medic_move_run = { FRAME_run1, FRAME_run6, medic_frames_run, NULL };

static void medic_run (edict_t *self)
{
	monster_done_dodge (self);

	if (!(self->monsterinfo.aiflags & AI_MEDIC))
	{
		if (medic_take_patient (self))
			return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &medic_move_stand;
	else
		self->monsterinfo.currentmove = &medic_move_run;
}

static mframe_t medic_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};
static mmove_t medic_move_pain1 = { FRAME_paina2, FRAME_paina6, medic_frames_pain1, medic_run };

static mframe_t medic_frames_pain2[] = {
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
	{ ai_move, 0, monster_footstep }
};
static mmove_t medic_move_pain2 = { FRAME_painb2, FRAME_painb13, medic_frames_pain2, medic_run };

static void medic_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	float	r;

	monster_done_dodge (self);

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	r = random();

	if (self->mass > 400)
	{
		if (damage < 35)
		{
			gi.sound (self, CHAN_VOICE, commander_sound_pain1, 1, ATTN_NORM, 0);
			if (meansOfDeath != MOD_CHAINFIST)
				return;
		}

		gi.sound (self, CHAN_VOICE, commander_sound_pain2, 1, ATTN_NORM, 0);
	}
	else if (r < 0.5f)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	// if we're healing someone, we ignore pain
	if (meansOfDeath != MOD_CHAINFIST && (self->monsterinfo.aiflags & AI_MEDIC))
		return;

	if (self->mass > 400)
	{
		self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

		if (r < (damage * 0.005f < 0.5f ? damage * 0.005f : 0.5f)) // no more than 50% chance of big pain
			self->monsterinfo.currentmove = &medic_move_pain2;
		else
			self->monsterinfo.currentmove = &medic_move_pain1;
	}
	else if (r < 0.5f)
		self->monsterinfo.currentmove = &medic_move_pain1;
	else
		self->monsterinfo.currentmove = &medic_move_pain2;

	// PMM - clear duck flag
	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up (self);

	abortHeal (self, false, false, false);
}

static void medic_setskin (edict_t *self)
{
	if ((self->health < (self->max_health / 2)))
		self->s.skinnum |= 1;
	else
		self->s.skinnum &= ~1;
}

static void medic_fire_blaster (edict_t *self)
{
	vec3_t	start;
	vec3_t	forward, right;
	vec3_t	end;
	vec3_t	dir;
	int		effect;
	int		damage = 2;
	int		mz;

	// paranoia checking
	if (!(self->enemy && self->enemy->inuse))
		return;

	if ((self->s.frame == FRAME_attack9) || (self->s.frame == FRAME_attack12))
	{
		effect = EF_BLASTER;
		damage = 6;
		mz = (self->mass > 400) ? MZ2_MEDIC_BLASTER_2 : MZ2_MEDIC_BLASTER_1;
	}
	else
	{
		effect = (self->s.frame % 4) ? 0 : EF_HYPERBLASTER;
		mz = ((self->mass > 400) ? MZ2_MEDIC_HYPERBLASTER2_1 : MZ2_MEDIC_HYPERBLASTER1_1) + (self->s.frame - FRAME_attack19);
	}

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[mz], forward, right, start);

	VectorCopy (self->enemy->s.origin, end);
	end[2] += self->enemy->viewheight;
	VectorSubtract (end, start, dir);
	VectorNormalize (dir);

	if (!strcmp (self->enemy->classname, "tesla_mine"))
		damage = 3;

	// medic commander shoots blaster2
	if (self->mass > 400)
		monster_fire_blaster2 (self, start, dir, damage, 1000, mz, effect);
	else
		monster_fire_blaster (self, start, dir, damage, 1000, mz, effect);
}

static void medic_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

static void medic_shrink (edict_t *self)
{
	self->maxs[2] = -2;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static mframe_t medic_frames_death[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, -18.f, monster_footstep },
	{ ai_move, -10.f, medic_shrink },
	{ ai_move, -6.f },
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
	{ ai_move }
};
static mmove_t medic_move_death = { FRAME_death2, FRAME_death30, medic_frames_death, medic_dead };

static gib_def_t medic_gibs[] = {
	{ 2, "models/objects/gibs/bone/tris.md2" },
	{ 1, "models/objects/gibs/sm_meat/tris.md2" },
	{ 1, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
	{ 1, "models/monsters/medic/gibs/chest.md2", GIB_SKINNED },
	{ 2, "models/monsters/medic/gibs/leg.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/medic/gibs/hook.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/medic/gibs/gun.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/medic/gibs/head.md2", GIB_SKINNED | GIB_HEAD },
	{ 0, NULL }
};

static void medic_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	// if we had a pending patient, he was already freed up in Killed

	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs (self, damage, medic_gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	//	PMM
	if (self->mass == 400)
		gi.sound (self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, commander_sound_die, 1, ATTN_NORM, 0);
	//
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	self->monsterinfo.currentmove = &medic_move_death;
}

static mframe_t medic_frames_duck[] = {
	{ ai_move, -1 },
	{ ai_move, -1, monster_duck_down },
	{ ai_move, -1, monster_duck_hold },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 }, // PMM - duck up used to be here
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1 },
	{ ai_move, -1, monster_duck_up }
};
static mmove_t medic_move_duck = { FRAME_duck2, FRAME_duck14, medic_frames_duck, medic_run };

// PMM -- moved dodge code to after attack code so I can reference attack frames

static mframe_t medic_frames_attackHyperBlaster[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge },
	{ ai_charge },
	// [Paril-KEX] end on 36 as intended
	{ ai_charge, 2.f }, // 33
	{ ai_charge, 3.f, monster_footstep },
};
static mmove_t medic_move_attackHyperBlaster = { FRAME_attack15, FRAME_attack34, medic_frames_attackHyperBlaster, medic_run };

static void medic_quick_attack (edict_t *self)
{
	if (random() < 0.5f)
	{
		self->monsterinfo.currentmove = &medic_move_attackHyperBlaster;
		self->monsterinfo.nextframe = FRAME_attack16;
	}
}

static void medic_continue (edict_t *self)
{
	if (visible (self, self->enemy))
		if (random() <= 0.95f)
			self->monsterinfo.currentmove = &medic_move_attackHyperBlaster;
}

static mframe_t medic_frames_attackBlaster[] = {
	{ ai_charge, 5 },
	{ ai_charge, 3 },
	{ ai_charge, 2 },
	{ ai_charge, 0, medic_quick_attack },
	{ ai_charge, 0, monster_footstep },
	{ ai_charge },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, medic_fire_blaster },
	{ ai_charge },
	{ ai_charge, 0, medic_continue } // Change to medic_continue... Else, go to frame 32
};
static mmove_t medic_move_attackBlaster = { FRAME_attack3, FRAME_attack14, medic_frames_attackBlaster, medic_run };

static void medic_hook_launch (edict_t *self)
{
	// PMM - commander sounds
	if (self->mass == 400)
		gi.sound (self, CHAN_WEAPON, sound_hook_launch, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_WEAPON, commander_sound_hook_launch, 1, ATTN_NORM, 0);
}

static vec3_t medic_cable_offsets[] = {
	{ 45.0f, -9.2f, 15.5f },
	{ 48.4f, -9.7f, 15.2f },
	{ 47.8f, -9.8f, 15.8f },
	{ 47.3f, -9.3f, 14.3f },
	{ 45.4f, -10.1f, 13.1f },
	{ 41.9f, -12.7f, 12.0f },
	{ 37.8f, -15.8f, 11.2f },
	{ 34.3f, -18.4f, 10.7f },
	{ 32.7f, -19.7f, 10.4f },
	{ 32.7f, -19.7f, 10.4f }
};

// raise its patient: as it was spawned, with the health it had
static void medic_raise (edict_t *self)
{
	edict_t		*patient = self->enemy;
	int			old_max_health = patient->max_health;
	int			old_gib_health = patient->gib_health;
	int			old_power_armor_type = patient->monsterinfo.power_armor_type;
	int			old_power_armor_power = patient->monsterinfo.power_armor_power;
	int			monster_slots = patient->monsterinfo.monster_slots;
	int			monster_used = patient->monsterinfo.monster_used;
	int			num_reinforcements = patient->monsterinfo.num_reinforcements;
	static monsterinfo_t	saved;	// for its reinforcements, which are inline

	memcpy (saved.reinforcements, patient->monsterinfo.reinforcements, sizeof(saved.reinforcements));

	patient->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

	memset (&st, 0, sizeof(st));
	ED_CallSpawn (patient);

	memcpy (patient->monsterinfo.reinforcements, saved.reinforcements, sizeof(saved.reinforcements));
	patient->monsterinfo.num_reinforcements = num_reinforcements;
	patient->monsterinfo.monster_slots = monster_slots;
	patient->monsterinfo.monster_used = monster_used;

	patient->gib_health = old_gib_health / 2;
	patient->health = patient->max_health = old_max_health;
	patient->monsterinfo.power_armor_type = old_power_armor_type;
	patient->monsterinfo.power_armor_power = old_power_armor_power;

	if (patient->monsterinfo.setskin)
		patient->monsterinfo.setskin (patient);

	if (patient->think)
	{
		patient->nextthink = level.time;
		patient->think (patient);
	}

	patient->monsterinfo.aiflags &= ~AI_RESURRECTING;
	patient->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT;
	// turn off flies
	patient->s.effects &= ~EF_FLIES;
	patient->monsterinfo.healer = NULL;

	if ((self->oldenemy) && (self->oldenemy->inuse) && (self->oldenemy->health > 0))
	{
		patient->enemy = self->oldenemy;
		FoundTarget (patient);
	}
	else
	{
		patient->enemy = NULL;
		if (!FindTarget (patient))
		{
			// no valid enemy, so stop acting
			patient->monsterinfo.pausetime = 100000000;
			patient->monsterinfo.stand (patient);
		}
		self->enemy = NULL;
		self->oldenemy = NULL;
		if (!FindTarget (self))
		{
			// no valid enemy, so stop acting
			self->monsterinfo.pausetime = 100000000;
			self->monsterinfo.stand (self);
			return;
		}
	}

	cleanupHeal (self, false);
}

static void medic_cable_attack (edict_t *self)
{
	vec3_t	offset, start, end, f, r;
	trace_t	tr;
	vec3_t	dir, maxs;
	float	distance;

	if ((!self->enemy) || (!self->enemy->inuse) || (self->enemy->s.effects & EF_GIB))
	{
		abortHeal (self, false, false, false);
		return;
	}

	// we switched back to a player; let the animation finish
	if (self->enemy->client)
		return;

	// see if our enemy has changed to a client, or our target has more than 0 health,
	// abort it .. we got switched to someone else due to damage
	if (self->enemy->health > 0)
	{
		abortHeal (self, false, false, false);
		return;
	}

	AngleVectors (self->s.angles, f, r, NULL);
	VectorCopy (medic_cable_offsets[self->s.frame - FRAME_attack42], offset);
	G_ProjectSource (self->s.origin, offset, f, r, start);

	// check for max distance
	// not needed, done in checkattack
	// check for min distance
	VectorSubtract (start, self->enemy->s.origin, dir);
	distance = VectorLength (dir);
	if (distance < MEDIC_MIN_DISTANCE)
	{
		abortHeal (self, true, true, false);
		return;
	}

	tr = gi.trace (start, NULL, NULL, self->enemy->s.origin, self, MASK_SOLID);
	if (tr.fraction != 1.0f && tr.ent != self->enemy)
	{
		if (tr.ent == world)
		{
			// give up on second try
			if (self->monsterinfo.medicTries > 1)
			{
				abortHeal (self, true, false, true);
				return;
			}
			self->monsterinfo.medicTries++;
			cleanupHeal (self, 1);
			return;
		}
		abortHeal (self, true, false, false);
		return;
	}

	if (self->s.frame == FRAME_attack43)
	{
		// PMM - commander sounds
		if (self->mass == 400)
			gi.sound (self->enemy, CHAN_AUTO, sound_hook_hit, 1, ATTN_NORM, 0);
		else
			gi.sound (self->enemy, CHAN_AUTO, commander_sound_hook_hit, 1, ATTN_NORM, 0);

		self->enemy->monsterinfo.aiflags |= AI_RESURRECTING;
		self->enemy->takedamage = DAMAGE_NO;
		M_SetEffects (self->enemy);
	}
	else if (self->s.frame == FRAME_attack50)
	{
		self->enemy->spawnflags = 0;
		self->enemy->monsterinfo.aiflags &= AI_STINKY | AI_SPAWNED_CARRIER | AI_SPAWNED_MEDIC_C | AI_SPAWNED_WIDOW;
		self->enemy->target = NULL;
		self->enemy->targetname = NULL;
		self->enemy->combattarget = NULL;
		self->enemy->deathtarget = NULL;
		self->enemy->monsterinfo.healer = self;

		VectorCopy (self->enemy->maxs, maxs);
		maxs[2] += 48; // compensate for change when they die

		// down a unit onto where it lies: a box only resting on the floor
		// is not in it, as a trace that does not move here would have it
		VectorCopy (self->enemy->s.origin, start);
		start[2] += 1;
		tr = gi.trace (start, self->enemy->mins, maxs, self->enemy->s.origin, self->enemy, MASK_MONSTERSOLID);

		if (tr.startsolid || tr.allsolid)
		{
			abortHeal (self, true, true, false);
			return;
		}
		else if (tr.ent != world)
		{
			abortHeal (self, true, true, false);
			return;
		}

		medic_raise (self);
		return;
	}
	else
	{
		if (self->s.frame == FRAME_attack44)
		{
			// PMM - medic commander sounds
			if (self->mass == 400)
				gi.sound (self, CHAN_WEAPON, sound_hook_heal, 1, ATTN_NORM, 0);
			else
				gi.sound (self, CHAN_WEAPON, commander_sound_hook_heal, 1, ATTN_NORM, 0);
		}
	}

	// adjust start for beam origin being in middle of a segment
	VectorMA (start, 8, f, start);

	// adjust end z for end spot since the monster is currently dead
	VectorCopy (self->enemy->s.origin, end);
	end[2] = (self->enemy->absmin[2] + self->enemy->absmax[2]) / 2;

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_MEDIC_CABLE_ATTACK);
	gi.WriteShort (self - g_edicts);
	gi.WritePosition (start);
	gi.WritePosition (end);
	gi.multicast (self->s.origin, MULTICAST_PVS);
}

static void medic_hook_retract (edict_t *self)
{
	if (self->mass == 400)
		gi.sound (self, CHAN_WEAPON, sound_hook_retract, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_WEAPON, sound_hook_retract, 1, ATTN_NORM, 0);

	self->monsterinfo.aiflags &= ~AI_MEDIC;

	if (self->oldenemy && self->oldenemy->inuse && self->oldenemy->health > 0)
	{
		self->enemy = self->oldenemy;
		HuntTarget (self);
	}
	else
	{
		self->enemy = self->goalentity = NULL;
		self->oldenemy = NULL;
		if (!FindTarget (self))
		{
			// no valid enemy, so stop acting
			self->monsterinfo.pausetime = 100000000;
			self->monsterinfo.stand (self);
			return;
		}
	}
}

static mframe_t medic_frames_attackCable[] = {
	// ROGUE - negated 36-40 so he scoots back from his target a little
	// ROGUE - switched 33-36 to ai_charge
	// ROGUE - changed frame 52 to 60 to compensate for changes in 36-40
	// [Paril-KEX] started on 36 as they intended
	{ ai_charge, -4.7f }, // 37
	{ ai_charge, -5.f },
	{ ai_charge, -6.f },
	{ ai_charge, -4.f }, // 40
	{ ai_charge, 0, monster_footstep },
	{ ai_move, 0, medic_hook_launch },	// 42
	{ ai_move, 0, medic_cable_attack }, // 43
	{ ai_move, 0, medic_cable_attack },
	{ ai_move, 0, medic_cable_attack },
	{ ai_move, 0, medic_cable_attack },
	{ ai_move, 0, medic_cable_attack },
	{ ai_move, 0, medic_cable_attack },
	{ ai_move, 0, medic_cable_attack },
	{ ai_move, 0, medic_cable_attack },
	{ ai_move, 0, medic_cable_attack }, // 51
	{ ai_move, 0, medic_hook_retract }, // 52
	{ ai_move, -1.5f },
	{ ai_move, -1.2f, monster_footstep },
	{ ai_move, -3.f }
};
static mmove_t medic_move_attackCable = { FRAME_attack37, FRAME_attack55, medic_frames_attackCable, medic_run };

static void medic_start_spawn (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, commander_sound_spawn, 1, ATTN_NORM, 0);
	self->monsterinfo.nextframe = FRAME_attack48;
}

static mframe_t medic_frames_callReinforcements[] = {
	// ROGUE - 33-36 now ai_charge
	{ ai_charge, 2 }, // 33
	{ ai_charge, 3 },
	{ ai_charge, 5 },
	{ ai_charge, 4.4f }, // 36
	{ ai_charge, 4.7f },
	{ ai_charge, 5 },
	{ ai_charge, 6 },
	{ ai_charge, 4 }, // 40
	{ ai_charge, 0, monster_footstep },
	{ ai_move, 0, medic_start_spawn }, // 42
	{ ai_move },					   // 43 -- 43 through 47 are skipped
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, medic_determine_spawn }, // 48
	{ ai_charge, 0, medic_spawngrows },	   // 49
	{ ai_move },						   // 50
	{ ai_move },						   // 51
	{ ai_move, -15, medic_finish_spawn },  // 52
	{ ai_move, -1.5f },
	{ ai_move, -1.2f },
	{ ai_move, -3, monster_footstep }
};
static mmove_t medic_move_callReinforcements = { FRAME_attack33, FRAME_attack55, medic_frames_callReinforcements, medic_run };

static void medic_attack (edict_t *self)
{
	float	r;

	monster_done_dodge (self);

	// signal from checkattack to spawn
	if (self->monsterinfo.aiflags & AI_BLOCKED)
	{
		self->monsterinfo.currentmove = &medic_move_callReinforcements;
		self->monsterinfo.aiflags &= ~AI_BLOCKED;
	}

	r = random();
	if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		if ((self->mass > 400) && (r > 0.8f) && M_SlotsLeft (self))
			self->monsterinfo.currentmove = &medic_move_callReinforcements;
		else
			self->monsterinfo.currentmove = &medic_move_attackCable;
	}
	else
	{
		if (self->monsterinfo.attack_state == AS_BLIND)
		{
			self->monsterinfo.currentmove = &medic_move_callReinforcements;
			return;
		}
		if ((self->mass > 400) && (r > 0.2f) && (range_to (self, self->enemy) > RANGE_MELEE_RR) && M_SlotsLeft (self))
			self->monsterinfo.currentmove = &medic_move_callReinforcements;
		else
			self->monsterinfo.currentmove = &medic_move_attackBlaster;
	}
}

static qboolean medic_checkattack (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		// if our target went away
		if ((!self->enemy) || (!self->enemy->inuse))
		{
			abortHeal (self, true, false, false);
			return false;
		}

		// if we ran out of time, give up
		if (self->timestamp < level.time)
		{
			abortHeal (self, true, false, true);
			self->timestamp = 0;
			return false;
		}

		if (realrange (self, self->enemy) < MEDIC_MAX_HEAL_DISTANCE + 10)
		{
			medic_attack (self);
			return true;
		}
		else
		{
			self->monsterinfo.attack_state = AS_STRAIGHT;
			return false;
		}
	}

	if (self->enemy->client && !visible (self, self->enemy) && M_SlotsLeft (self))
	{
		self->monsterinfo.attack_state = AS_BLIND;
		return true;
	}

	// give a LARGE bias to spawning things when we have room
	// use AI_BLOCKED as a signal to attack to spawn
	if (self->monsterinfo.monster_slots && (random() < 0.8f) && (M_SlotsLeft (self) > self->monsterinfo.monster_slots * 0.8f) && (realrange (self, self->enemy) > 150))
	{
		self->monsterinfo.aiflags |= AI_BLOCKED;
		self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	// ROGUE
	// since his idle animation looks kinda bad in combat, always attack
	// when he's on a combat point
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	return M_CheckAttack (self);
}

static qboolean medic_duck (edict_t *self, float eta)
{
	//	don't dodge if you're healing
	if (self->monsterinfo.aiflags & AI_MEDIC)
		return false;

	if ((self->monsterinfo.currentmove == &medic_move_attackHyperBlaster) ||
		(self->monsterinfo.currentmove == &medic_move_attackCable) ||
		(self->monsterinfo.currentmove == &medic_move_attackBlaster) ||
		(self->monsterinfo.currentmove == &medic_move_callReinforcements))
	{
		// he ignores skill
		self->monsterinfo.unduck (self);
		return false;
	}

	self->monsterinfo.currentmove = &medic_move_duck;

	return true;
}

static qboolean medic_sidestep (edict_t *self)
{
	if ((self->monsterinfo.currentmove == &medic_move_attackHyperBlaster) ||
		(self->monsterinfo.currentmove == &medic_move_attackCable) ||
		(self->monsterinfo.currentmove == &medic_move_attackBlaster) ||
		(self->monsterinfo.currentmove == &medic_move_callReinforcements))
	{
		// if we're shooting, don't dodge
		return false;
	}

	if (self->monsterinfo.currentmove != &medic_move_run)
		self->monsterinfo.currentmove = &medic_move_run;

	return true;
}

//===========
// PGM
static qboolean medic_blocked (edict_t *self, float dist)
{
	return blocked_checkplat (self, dist);
}
// PGM
//===========

void SP_monster_medic_rr (edict_t *self)
{
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/medic/tris.md2");

	gi.modelindex ("models/monsters/medic/gibs/chest.md2");
	gi.modelindex ("models/monsters/medic/gibs/gun.md2");
	gi.modelindex ("models/monsters/medic/gibs/head.md2");
	gi.modelindex ("models/monsters/medic/gibs/hook.md2");
	gi.modelindex ("models/monsters/medic/gibs/leg.md2");

	VectorSet (self->mins, -24, -24, -24);
	VectorSet (self->maxs, 24, 24, 32);

	// PMM
	if (strcmp (self->classname, "monster_medic_commander") == 0)
	{
		self->health = 600;
		self->gib_health = -130;
		self->mass = 600;
		self->yaw_speed = 40; // default is 20
		gi.modelindex ("models/items/spawngro/tris.md2");
		gi.modelindex ("models/items/spawngro2/tris.md2");
	}
	else
	{
		// PMM
		self->health = 300;
		self->gib_health = -130;
		self->mass = 400;
	}

	self->pain = medic_pain;
	self->die = medic_die;

	self->monsterinfo.stand = medic_stand;
	self->monsterinfo.walk = medic_walk;
	self->monsterinfo.run = medic_run;
	// pmm
	self->monsterinfo.dodge = (void (*)(edict_t *, edict_t *, float))M_MonsterDodge;
	self->monsterinfo.duck = medic_duck;
	self->monsterinfo.unduck = monster_duck_up;
	self->monsterinfo.sidestep = medic_sidestep;
	self->monsterinfo.blocked = medic_blocked;
	// pmm
	self->monsterinfo.attack = medic_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = medic_sight;
	self->monsterinfo.idle = medic_idle;
	self->monsterinfo.search = medic_search;
	self->monsterinfo.checkattack = medic_checkattack;
	self->monsterinfo.setskin = medic_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &medic_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start (self);

	// PMM
	self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

	if (self->mass > 400)
	{
		self->s.skinnum = 2;

		// commander sounds
		commander_sound_idle1 = gi.soundindex ("medic_commander/medidle.wav");
		commander_sound_pain1 = gi.soundindex ("medic_commander/medpain1.wav");
		commander_sound_pain2 = gi.soundindex ("medic_commander/medpain2.wav");
		commander_sound_die = gi.soundindex ("medic_commander/meddeth.wav");
		commander_sound_sight = gi.soundindex ("medic_commander/medsght.wav");
		commander_sound_search = gi.soundindex ("medic_commander/medsrch.wav");
		commander_sound_hook_launch = gi.soundindex ("medic_commander/medatck2c.wav");
		commander_sound_hook_hit = gi.soundindex ("medic_commander/medatck3a.wav");
		commander_sound_hook_heal = gi.soundindex ("medic_commander/medatck4a.wav");
		commander_sound_hook_retract = gi.soundindex ("medic_commander/medatck5a.wav");
		commander_sound_spawn = gi.soundindex ("medic_commander/monsterspawn1.wav");
		gi.soundindex ("tank/tnkatck3.wav");

		// its reinforcements: the map's, or soldiers to a gladiator
		M_SetupReinforcements (self, default_reinforcements, DEFAULT_MONSTER_SLOTS_BASE);
		self->monsterinfo.blindfire = true;
	}
	else
	{
		sound_idle1 = gi.soundindex ("medic/idle.wav");
		sound_pain1 = gi.soundindex ("medic/medpain1.wav");
		sound_pain2 = gi.soundindex ("medic/medpain2.wav");
		sound_die = gi.soundindex ("medic/meddeth1.wav");
		sound_sight = gi.soundindex ("medic/medsght1.wav");
		sound_search = gi.soundindex ("medic/medsrch1.wav");
		sound_hook_launch = gi.soundindex ("medic/medatck2.wav");
		sound_hook_hit = gi.soundindex ("medic/medatck3.wav");
		sound_hook_heal = gi.soundindex ("medic/medatck4.wav");
		sound_hook_retract = gi.soundindex ("medic/medatck5.wav");

		gi.soundindex ("medic/medatck1.wav");

		self->s.skinnum = 0;
	}
	// pmm
}
