// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

BRAIN, the 2023 re-release's

Its m_brain.cpp, in C, for its own maps (RR_Map): a brain whose power
screen is down only while its chest is open, that claws and lashes its
tentacles up close, sucks you in with its tongue from a little further,
and from afar sweeps you with lasers from both eyes, held on you as they
lead you; that ducks, and gibs into pieces of its own. Its frames are
1997's. m_brain.c is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10.
==============================================================================
*/

#include "g_local.h"
#include "m_brain.h"

qboolean visible (edict_t *self, edict_t *other);

#define SPAWNFLAG_BRAIN_NO_LASERS	8

static int	sound_chest_open;
static int	sound_tentacles_extend;
static int	sound_tentacles_retract;
static int	sound_death;
static int	sound_idle1;
static int	sound_idle2;
static int	sound_idle3;
static int	sound_pain1;
static int	sound_pain2;
static int	sound_sight;
static int	sound_search;
static int	sound_melee1;
static int	sound_melee2;
static int	sound_melee3;

static void brain_run (edict_t *self);
static void brain_dead (edict_t *self);
static void brain_stand (edict_t *self);
static void brain_shrink (edict_t *self);

static void brain_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}

static void brain_search (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

static void brain_duck_down_step (edict_t *self)
{
	monster_duck_down (self);
	monster_footstep (self);
}

static void brain_shrink_step (edict_t *self)
{
	brain_shrink (self);
	monster_footstep (self);
}

//
// STAND
//

static mframe_t brain_frames_stand[] = {
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
static mmove_t brain_move_stand = { FRAME_stand01, FRAME_stand30, brain_frames_stand, NULL };

static void brain_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &brain_move_stand;
}

//
// IDLE
//

static mframe_t brain_frames_idle[] = {
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
static mmove_t brain_move_idle = { FRAME_stand31, FRAME_stand60, brain_frames_idle, brain_stand };

static void brain_idle (edict_t *self)
{
	gi.sound (self, CHAN_AUTO, sound_idle3, 1, ATTN_IDLE, 0);
	self->monsterinfo.currentmove = &brain_move_idle;
}

//
// WALK
//

static mframe_t brain_frames_walk1[] = {
	{ ai_walk, 7 },
	{ ai_walk, 2 },
	{ ai_walk, 3 },
	{ ai_walk, 3, monster_footstep },
	{ ai_walk, 1 },
	{ ai_walk },
	{ ai_walk },
	{ ai_walk, 9 },
	{ ai_walk, -4 },
	{ ai_walk, -1, monster_footstep },
	{ ai_walk, 2 }
};
static mmove_t brain_move_walk1 = { FRAME_walk101, FRAME_walk111, brain_frames_walk1, NULL };

static void brain_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &brain_move_walk1;
}

static mframe_t brain_frames_pain3[] = {
	{ ai_move, -2 },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 3 },
	{ ai_move },
	{ ai_move, -4 }
};
static mmove_t brain_move_pain3 = { FRAME_pain301, FRAME_pain306, brain_frames_pain3, brain_run };

static mframe_t brain_frames_pain2[] = {
	{ ai_move, -2 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move, 1 },
	{ ai_move, -2 }
};
static mmove_t brain_move_pain2 = { FRAME_pain201, FRAME_pain208, brain_frames_pain2, brain_run };

static mframe_t brain_frames_pain1[] = {
	{ ai_move, -6 },
	{ ai_move, -2 },
	{ ai_move, -6, monster_footstep },
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
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 1 },
	{ ai_move, 7 },
	{ ai_move },
	{ ai_move, 3, monster_footstep },
	{ ai_move, -1 }
};
static mmove_t brain_move_pain1 = { FRAME_pain101, FRAME_pain121, brain_frames_pain1, brain_run };

static mframe_t brain_frames_duck[] = {
	{ ai_move },
	{ ai_move, -2, brain_duck_down_step },
	{ ai_move, 17, monster_duck_hold },
	{ ai_move, -3 },
	{ ai_move, -1, monster_duck_up },
	{ ai_move, -5 },
	{ ai_move, -6 },
	{ ai_move, -6, monster_footstep }
};
static mmove_t brain_move_duck = { FRAME_duck01, FRAME_duck08, brain_frames_duck, brain_run };

static void brain_shrink (edict_t *self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static mframe_t brain_frames_death2[] = {
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move, 0, brain_shrink },
	{ ai_move, 9 },
	{ ai_move }
};
static mmove_t brain_move_death2 = { FRAME_death201, FRAME_death205, brain_frames_death2, brain_dead };

static mframe_t brain_frames_death1[] = {
	{ ai_move },
	{ ai_move, 0, monster_footstep },
	{ ai_move, -2 },
	{ ai_move, 9, brain_shrink_step },
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
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move }
};
static mmove_t brain_move_death1 = { FRAME_death101, FRAME_death118, brain_frames_death1, brain_dead };

//
// MELEE
//

static void brain_swing_right (edict_t *self)
{
	gi.sound (self, CHAN_BODY, sound_melee1, 1, ATTN_NORM, 0);
}

static void brain_hit_right (edict_t *self)
{
	vec3_t	aim;

	VectorSet (aim, MELEE_DISTANCE_RR, self->maxs[0], 8);
	if (fire_hit_rr (self, aim, 15 + rand () % 5, 40))
		gi.sound (self, CHAN_WEAPON, sound_melee3, 1, ATTN_NORM, 0);
	else
		self->monsterinfo.melee_debounce_time = level.time + 3;
}

static void brain_swing_left (edict_t *self)
{
	gi.sound (self, CHAN_BODY, sound_melee2, 1, ATTN_NORM, 0);
}

static void brain_hit_left (edict_t *self)
{
	vec3_t	aim;

	VectorSet (aim, MELEE_DISTANCE_RR, self->mins[0], 8);
	if (fire_hit_rr (self, aim, 15 + rand () % 5, 40))
		gi.sound (self, CHAN_WEAPON, sound_melee3, 1, ATTN_NORM, 0);
	else
		self->monsterinfo.melee_debounce_time = level.time + 3;
}

static mframe_t brain_frames_attack1[] = {
	{ ai_charge, 8 },
	{ ai_charge, 3 },
	{ ai_charge, 5 },
	{ ai_charge, 0, monster_footstep },
	{ ai_charge, -3, brain_swing_right },
	{ ai_charge },
	{ ai_charge, -5 },
	{ ai_charge, -7, brain_hit_right },
	{ ai_charge },
	{ ai_charge, 6, brain_swing_left },
	{ ai_charge, 1 },
	{ ai_charge, 2, brain_hit_left },
	{ ai_charge, -3 },
	{ ai_charge, 6 },
	{ ai_charge, -1 },
	{ ai_charge, -3 },
	{ ai_charge, 2 },
	{ ai_charge, -11, monster_footstep }
};
static mmove_t brain_move_attack1 = { FRAME_attak101, FRAME_attak118, brain_frames_attack1, brain_run };

static void brain_chest_open (edict_t *self)
{
	self->count = 0;
	self->monsterinfo.power_armor_type = POWER_ARMOR_NONE;
	gi.sound (self, CHAN_BODY, sound_chest_open, 1, ATTN_NORM, 0);
}

static void brain_tentacle_attack (edict_t *self)
{
	vec3_t	aim;

	VectorSet (aim, MELEE_DISTANCE_RR, 0, 8);
	if (fire_hit_rr (self, aim, 10 + rand () % 5, -600))
		self->count = 1;
	else
		self->monsterinfo.melee_debounce_time = level.time + 3;
	gi.sound (self, CHAN_WEAPON, sound_tentacles_retract, 1, ATTN_NORM, 0);
}

static void brain_chest_closed (edict_t *self)
{
	self->monsterinfo.power_armor_type = POWER_ARMOR_SCREEN;
	if (self->count)
	{
		self->count = 0;
		self->monsterinfo.currentmove = &brain_move_attack1;
	}
}

static qboolean brain_tounge_attack_ok (vec3_t start, vec3_t end)
{
	vec3_t	dir, angles;

	// check for max distance
	VectorSubtract (start, end, dir);
	if (VectorLength (dir) > 512)
		return false;

	// check for min/max pitch
	vectoangles (dir, angles);
	if (angles[0] < -180)
		angles[0] += 360;
	if (fabs (angles[0]) > 30)
		return false;

	return true;
}

static void brain_tounge_attack (edict_t *self)
{
	vec3_t	offset, start, f, r, end, dir, forward;
	trace_t	tr;
	int		damage;

	AngleVectors (self->s.angles, f, r, NULL);
	// offset = { 24, 0, 6 };
	VectorSet (offset, 24, 0, 16);
	G_ProjectSource (self->s.origin, offset, f, r, start);

	VectorCopy (self->enemy->s.origin, end);
	if (!brain_tounge_attack_ok (start, end))
	{
		end[2] = self->enemy->s.origin[2] + self->enemy->maxs[2] - 8;
		if (!brain_tounge_attack_ok (start, end))
		{
			end[2] = self->enemy->s.origin[2] + self->enemy->mins[2] + 8;
			if (!brain_tounge_attack_ok (start, end))
				return;
		}
	}
	VectorCopy (self->enemy->s.origin, end);

	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);
	if (tr.ent != self->enemy)
		return;

	damage = 5;
	gi.sound (self, CHAN_WEAPON, sound_tentacles_retract, 1, ATTN_NORM, 0);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_PARASITE_ATTACK);
	gi.WriteShort (self - g_edicts);
	gi.WritePosition (start);
	gi.WritePosition (end);
	gi.multicast (self->s.origin, MULTICAST_PVS);

	VectorSubtract (start, end, dir);
	T_Damage (self->enemy, self, self, dir, self->enemy->s.origin, vec3_origin, damage, 0, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN);

	// pull the enemy in
	self->s.origin[2] += 1;
	AngleVectors (self->s.angles, forward, NULL, NULL);
	VectorScale (forward, -1200, self->enemy->velocity);
}

// Brian right eye center
static vec3_t brain_reye[] = {
	{ 0.746700f, 0.238370f, 34.167690f },
	{ -1.076390f, 0.238370f, 33.386372f },
	{ -1.335500f, 5.334300f, 32.177170f },
	{ -0.175360f, 8.846370f, 30.635479f },
	{ -2.757590f, 7.804610f, 30.150860f },
	{ -5.575090f, 5.152840f, 30.056160f },
	{ -7.017550f, 3.262470f, 30.552521f },
	{ -7.915740f, 0.638800f, 33.176189f },
	{ -3.915390f, 8.285730f, 33.976349f },
	{ -0.913540f, 10.933030f, 34.141811f },
	{ -0.369900f, 8.923900f, 34.189079f }
};

// Brain left eye center
static vec3_t brain_leye[] = {
	{ -3.364710f, 0.327750f, 33.938381f },
	{ -5.140450f, 0.493480f, 32.659851f },
	{ -5.341980f, 5.646980f, 31.277901f },
	{ -4.134480f, 9.277440f, 29.925621f },
	{ -6.598340f, 6.815090f, 29.322620f },
	{ -8.610840f, 2.529650f, 29.251591f },
	{ -9.231360f, 0.093280f, 29.747959f },
	{ -11.004110f, 1.936930f, 32.395260f },
	{ -7.878310f, 7.648190f, 33.148151f },
	{ -4.947370f, 11.430050f, 33.313610f },
	{ -4.332820f, 9.444570f, 33.526340f }
};

// an eye's laser from where that eye is in this frame (of its walk)
static void brain_eye_laser_update (edict_t *laser, vec3_t *eye)
{
	edict_t	*self = laser->owner;
	vec3_t	start, forward, right, up, dir;
	int		n = self->s.frame - FRAME_walk101;

	if (n < 0 || n > FRAME_walk111 - FRAME_walk101)
		n = 0;

	// check for max distance
	AngleVectors (self->s.angles, forward, right, up);

	VectorMA (self->s.origin, eye[n][0], right, start);
	VectorMA (start, eye[n][1], forward, start);
	VectorMA (start, eye[n][2], up, start);

	PredictAim (self, self->enemy, start, 0, false, 0.1f + 0.1f * random(), dir, NULL);

	VectorCopy (start, laser->s.origin);
	VectorCopy (dir, laser->movedir);
	gi.linkentity (laser);
}

static void brain_right_eye_laser_update (edict_t *laser)
{
	// dis is my right eye
	brain_eye_laser_update (laser, brain_reye);
}

static void brain_left_eye_laser_update (edict_t *laser)
{
	brain_eye_laser_update (laser, brain_leye);
	dabeam_update_rr (laser, false);
}

static void brain_laserbeam (edict_t *self)
{
	// dis is my right eye
	monster_fire_dabeam_rr (self, 1, false, brain_right_eye_laser_update);

	// dis is me left eye
	monster_fire_dabeam_rr (self, 1, true, brain_left_eye_laser_update);
}

static void brain_laserbeam_reattack (edict_t *self)
{
	if (random() < 0.5f)
		if (visible (self, self->enemy))
			if (self->enemy->health > 0)
				self->s.frame = FRAME_walk101;
}

static mframe_t brain_frames_attack2[] = {
	{ ai_charge, 5 },
	{ ai_charge, -4 },
	{ ai_charge, -4 },
	{ ai_charge, -3 },
	{ ai_charge, 0, brain_chest_open },
	{ ai_charge },
	{ ai_charge, 13, brain_tentacle_attack },
	{ ai_charge },
	{ ai_charge, 2 },
	{ ai_charge },
	{ ai_charge, -9, brain_chest_closed },
	{ ai_charge },
	{ ai_charge, 4 },
	{ ai_charge, 3 },
	{ ai_charge, 2 },
	{ ai_charge, -3 },
	{ ai_charge, -6 }
};
static mmove_t brain_move_attack2 = { FRAME_attak201, FRAME_attak217, brain_frames_attack2, brain_run };

static void brain_melee (edict_t *self)
{
	if (random() <= 0.5f)
		self->monsterinfo.currentmove = &brain_move_attack1;
	else
		self->monsterinfo.currentmove = &brain_move_attack2;
}

static mframe_t brain_frames_attack3[] = {
	{ ai_charge, 5 },
	{ ai_charge, -4 },
	{ ai_charge, -4 },
	{ ai_charge, -3 },
	{ ai_charge, 0, brain_chest_open },
	{ ai_charge, 0, brain_tounge_attack },
	{ ai_charge, 13 },
	{ ai_charge, 0, brain_tentacle_attack },
	{ ai_charge, 2 },
	{ ai_charge, 0, brain_tounge_attack },
	{ ai_charge, -9, brain_chest_closed },
	{ ai_charge },
	{ ai_charge, 4 },
	{ ai_charge, 3 },
	{ ai_charge, 2 },
	{ ai_charge, -3 },
	{ ai_charge, -6 }
};
static mmove_t brain_move_attack3 = { FRAME_attak201, FRAME_attak217, brain_frames_attack3, brain_run };

static mframe_t brain_frames_attack4[] = {
	{ ai_charge, 9, brain_laserbeam },
	{ ai_charge, 2, brain_laserbeam },
	{ ai_charge, 3, brain_laserbeam },
	{ ai_charge, 3, brain_laserbeam },
	{ ai_charge, 1, brain_laserbeam },
	{ ai_charge, 0, brain_laserbeam },
	{ ai_charge, 0, brain_laserbeam },
	{ ai_charge, 10, brain_laserbeam },
	{ ai_charge, -4, brain_laserbeam },
	{ ai_charge, -1, brain_laserbeam },
	{ ai_charge, 2, brain_laserbeam_reattack }
};
static mmove_t brain_move_attack4 = { FRAME_walk101, FRAME_walk111, brain_frames_attack4, brain_run };

// RAFAEL
static void brain_attack (edict_t *self)
{
	float	r = range_to (self, self->enemy);

	if (r <= RANGE_NEAR_RR)
	{
		if (random() < 0.5f)
			self->monsterinfo.currentmove = &brain_move_attack3;
		else if (!(self->spawnflags & SPAWNFLAG_BRAIN_NO_LASERS))
			self->monsterinfo.currentmove = &brain_move_attack4;
	}
	else if (!(self->spawnflags & SPAWNFLAG_BRAIN_NO_LASERS))
		self->monsterinfo.currentmove = &brain_move_attack4;
}
// RAFAEL

//
// RUN
//

static mframe_t brain_frames_run[] = {
	{ ai_run, 9 },
	{ ai_run, 2 },
	{ ai_run, 3 },
	{ ai_run, 3 },
	{ ai_run, 1 },
	{ ai_run },
	{ ai_run },
	{ ai_run, 10 },
	{ ai_run, -4 },
	{ ai_run, -1 },
	{ ai_run, 2 }
};
static mmove_t brain_move_run = { FRAME_walk101, FRAME_walk111, brain_frames_run, NULL };

static void brain_run (edict_t *self)
{
	self->monsterinfo.power_armor_type = POWER_ARMOR_SCREEN;
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &brain_move_stand;
	else
		self->monsterinfo.currentmove = &brain_move_run;
}

static void brain_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	float	r;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	r = random();

	if (r < 0.33f)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else if (r < 0.66f)
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	if (r < 0.33f)
		self->monsterinfo.currentmove = &brain_move_pain1;
	else if (r < 0.66f)
		self->monsterinfo.currentmove = &brain_move_pain2;
	else
		self->monsterinfo.currentmove = &brain_move_pain3;

	// PMM - clear duck flag
	if (self->monsterinfo.aiflags & AI_DUCKED)
		monster_duck_up (self);
}

static void brain_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

static void brain_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

static gib_def_t brain_gibs[] = {
	{ 1, "models/objects/gibs/bone/tris.md2" },
	{ 2, "models/objects/gibs/sm_meat/tris.md2" },
	{ 2, "models/monsters/brain/gibs/arm.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/brain/gibs/boot.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/brain/gibs/pelvis.md2", GIB_SKINNED },
	{ 1, "models/monsters/brain/gibs/chest.md2", GIB_SKINNED },
	{ 2, "models/monsters/brain/gibs/door.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/brain/gibs/head.md2", GIB_SKINNED | GIB_HEAD },
	{ 0, NULL }
};

static void brain_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	self->s.effects = 0;
	self->monsterinfo.power_armor_type = POWER_ARMOR_NONE;

	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		if (self->beam)
		{
			G_FreeEdict (self->beam);
			self->beam = NULL;
		}
		if (self->beam2)
		{
			G_FreeEdict (self->beam2);
			self->beam2 = NULL;
		}

		ThrowGibs (self, damage, brain_gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	gi.sound (self, CHAN_VOICE, sound_death, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
	if (random() <= 0.5f)
		self->monsterinfo.currentmove = &brain_move_death1;
	else
		self->monsterinfo.currentmove = &brain_move_death2;
}

static qboolean brain_duck (edict_t *self, float eta)
{
	self->monsterinfo.currentmove = &brain_move_duck;
	return true;
}

void SP_monster_brain_rr (edict_t *self)
{
	sound_chest_open = gi.soundindex ("brain/brnatck1.wav");
	sound_tentacles_extend = gi.soundindex ("brain/brnatck2.wav");
	sound_tentacles_retract = gi.soundindex ("brain/brnatck3.wav");
	sound_death = gi.soundindex ("brain/brndeth1.wav");
	sound_idle1 = gi.soundindex ("brain/brnidle1.wav");
	sound_idle2 = gi.soundindex ("brain/brnidle2.wav");
	sound_idle3 = gi.soundindex ("brain/brnlens1.wav");
	sound_pain1 = gi.soundindex ("brain/brnpain1.wav");
	sound_pain2 = gi.soundindex ("brain/brnpain2.wav");
	sound_sight = gi.soundindex ("brain/brnsght1.wav");
	sound_search = gi.soundindex ("brain/brnsrch1.wav");
	sound_melee1 = gi.soundindex ("brain/melee1.wav");
	sound_melee2 = gi.soundindex ("brain/melee2.wav");
	sound_melee3 = gi.soundindex ("brain/melee3.wav");

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/brain/tris.md2");

	gi.modelindex ("models/monsters/brain/gibs/arm.md2");
	gi.modelindex ("models/monsters/brain/gibs/boot.md2");
	gi.modelindex ("models/monsters/brain/gibs/chest.md2");
	gi.modelindex ("models/monsters/brain/gibs/door.md2");
	gi.modelindex ("models/monsters/brain/gibs/head.md2");
	gi.modelindex ("models/monsters/brain/gibs/pelvis.md2");

	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, 32);

	self->health = 300;
	self->gib_health = -150;
	self->mass = 400;

	self->pain = brain_pain;
	self->die = brain_die;

	self->monsterinfo.stand = brain_stand;
	self->monsterinfo.walk = brain_walk;
	self->monsterinfo.run = brain_run;
	// PMM
	self->monsterinfo.dodge = (void (*)(edict_t *, edict_t *, float))M_MonsterDodge;
	self->monsterinfo.duck = brain_duck;
	self->monsterinfo.unduck = monster_duck_up;
	// pmm
	// RAFAEL
	self->monsterinfo.attack = brain_attack;
	// RAFAEL
	self->monsterinfo.melee = brain_melee;
	self->monsterinfo.sight = brain_sight;
	self->monsterinfo.search = brain_search;
	self->monsterinfo.idle = brain_idle;
	self->monsterinfo.setskin = brain_setskin;

	self->monsterinfo.power_armor_type = POWER_ARMOR_SCREEN;
	self->monsterinfo.power_armor_power = 100;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &brain_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start (self);
}
