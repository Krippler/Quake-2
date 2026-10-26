// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

ICARUS, the 2023 re-release's

Its m_hover.cpp, in C, for its own maps (RR_Map): the icarus and Ground
Zero's daedalus, which fly as the re-release's do (AI_ALTERNATE_FLY),
keeping their distance, fire their blasters now straight and now
circling, and as they die fall burning, bursting, till they hit the
ground and blow apart into pieces of their own. Their frames are 1997's.
m_hover.c is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10.
==============================================================================
*/

#include "g_local.h"
#include "m_hover.h"

qboolean visible (edict_t *self, edict_t *other);

static int	sound_pain1;
static int	sound_pain2;
static int	sound_death1;
static int	sound_death2;
static int	sound_sight;
static int	sound_search1;
static int	sound_search2;

static int	daed_sound_pain1;
static int	daed_sound_pain2;
static int	daed_sound_death1;
static int	daed_sound_death2;
static int	daed_sound_sight;
static int	daed_sound_search1;
static int	daed_sound_search2;

static void hover_run (edict_t *self);
static void hover_dead (edict_t *self);
static void hover_attack (edict_t *self);
static void hover_reattack (edict_t *self);
static void hover_fire_blaster (edict_t *self);

static void hover_sight (edict_t *self, edict_t *other)
{
	// PMM - daedalus sounds
	if (self->mass < 225)
		gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, daed_sound_sight, 1, ATTN_NORM, 0);
}

static void hover_search (edict_t *self)
{
	// PMM - daedalus sounds
	if (self->mass < 225)
	{
		if (random() < 0.5f)
			gi.sound (self, CHAN_VOICE, sound_search1, 1, ATTN_NORM, 0);
		else
			gi.sound (self, CHAN_VOICE, sound_search2, 1, ATTN_NORM, 0);
	}
	else
	{
		if (random() < 0.5f)
			gi.sound (self, CHAN_VOICE, daed_sound_search1, 1, ATTN_NORM, 0);
		else
			gi.sound (self, CHAN_VOICE, daed_sound_search2, 1, ATTN_NORM, 0);
	}
}

static mframe_t hover_frames_stand[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
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
static mmove_t hover_move_stand = { FRAME_stand01, FRAME_stand30, hover_frames_stand, NULL };

static mframe_t hover_frames_pain3[] = {
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
static mmove_t hover_move_pain3 = { FRAME_pain301, FRAME_pain309, hover_frames_pain3, hover_run };

static mframe_t hover_frames_pain2[] = {
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
static mmove_t hover_move_pain2 = { FRAME_pain201, FRAME_pain212, hover_frames_pain2, hover_run };

static mframe_t hover_frames_pain1[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, -8 },
	{ ai_move, -4 },
	{ ai_move, -6 },
	{ ai_move, -4 },
	{ ai_move, -3 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 3 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move, 3 },
	{ ai_move, 2 },
	{ ai_move, 7 },
	{ ai_move, 1 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 2 },
	{ ai_move },
	{ ai_move },
	{ ai_move, 5 },
	{ ai_move, 3 },
	{ ai_move, 4 }
};
static mmove_t hover_move_pain1 = { FRAME_pain101, FRAME_pain128, hover_frames_pain1, hover_run };

static mframe_t hover_frames_walk[] = {
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
static mmove_t hover_move_walk = { FRAME_forwrd01, FRAME_forwrd35, hover_frames_walk, NULL };

static mframe_t hover_frames_run[] = {
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 },
	{ ai_run, 10 }
};
static mmove_t hover_move_run = { FRAME_forwrd01, FRAME_forwrd35, hover_frames_run, NULL };

static gib_def_t hover_gibs[] = {
	{ 2, "models/objects/gibs/sm_meat/tris.md2" },
	{ 2, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
	{ 1, "models/monsters/hover/gibs/chest.md2", GIB_SKINNED },
	{ 2, "models/monsters/hover/gibs/ring.md2", GIB_SKINNED | GIB_METALLIC },
	{ 2, "models/monsters/hover/gibs/foot.md2", GIB_SKINNED },
	{ 1, "models/monsters/hover/gibs/head.md2", GIB_SKINNED | GIB_HEAD },
	{ 0, NULL }
};

static gib_def_t hover_meat[] = {
	{ 1, "models/objects/gibs/sm_meat/tris.md2" },
	{ 0, NULL }
};

static gib_def_t hover_metal[] = {
	{ 1, "models/objects/gibs/sm_metal/tris.md2", GIB_METALLIC },
	{ 0, NULL }
};

static void hover_gib (edict_t *self)
{
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_EXPLOSION1);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	self->s.skinnum /= 2;

	ThrowGibs (self, 150, hover_gibs);
}

static void hover_deadthink (edict_t *self)
{
	if (!self->groundentity && level.time < self->timestamp)
	{
		self->nextthink = level.time + FRAMETIME;
		return;
	}

	hover_gib (self);
}

static void hover_dying (edict_t *self)
{
	if (self->groundentity)
	{
		hover_deadthink (self);
		return;
	}

	if (rand () & 1)
		return;

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_PLAIN_EXPLOSION);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	if (rand () & 1)
		ThrowGibs (self, 120, hover_meat);
	else
		ThrowGibs (self, 120, hover_metal);
}

static mframe_t hover_frames_death1[] = {
	{ ai_move },
	{ ai_move, 0.f, hover_dying },
	{ ai_move },
	{ ai_move, 0.f, hover_dying },
	{ ai_move },
	{ ai_move, 0.f, hover_dying },
	{ ai_move, -10, hover_dying },
	{ ai_move, 3 },
	{ ai_move, 5, hover_dying },
	{ ai_move, 4, hover_dying },
	{ ai_move, 7 }
};
static mmove_t hover_move_death1 = { FRAME_death101, FRAME_death111, hover_frames_death1, hover_dead };

static mframe_t hover_frames_start_attack[] = {
	{ ai_charge, 1 },
	{ ai_charge, 1 },
	{ ai_charge, 1 }
};
static mmove_t hover_move_start_attack = { FRAME_attak101, FRAME_attak103, hover_frames_start_attack, hover_attack };

static mframe_t hover_frames_attack1[] = {
	{ ai_charge, -10, hover_fire_blaster },
	{ ai_charge, -10, hover_fire_blaster },
	{ ai_charge, 0, hover_reattack },
};
static mmove_t hover_move_attack1 = { FRAME_attak104, FRAME_attak106, hover_frames_attack1, NULL };

static mframe_t hover_frames_end_attack[] = {
	{ ai_charge, 1 },
	{ ai_charge, 1 }
};
static mmove_t hover_move_end_attack = { FRAME_attak107, FRAME_attak108, hover_frames_end_attack, hover_run };

/* PMM - circle strafing code */
static mframe_t hover_frames_attack2[] = {
	{ ai_charge, 10, hover_fire_blaster },
	{ ai_charge, 10, hover_fire_blaster },
	{ ai_charge, 10, hover_reattack },
};
static mmove_t hover_move_attack2 = { FRAME_attak104, FRAME_attak106, hover_frames_attack2, NULL };

static void hover_reattack (edict_t *self)
{
	if (self->enemy->health > 0)
		if (visible (self, self->enemy))
			if (random() <= 0.6f)
			{
				if (self->monsterinfo.attack_state == AS_STRAIGHT)
				{
					self->monsterinfo.currentmove = &hover_move_attack1;
					return;
				}
				else if (self->monsterinfo.attack_state == AS_SLIDING)
				{
					self->monsterinfo.currentmove = &hover_move_attack2;
					return;
				}
			}
	self->monsterinfo.currentmove = &hover_move_end_attack;
}

static void hover_fire_blaster (edict_t *self)
{
	vec3_t	start;
	vec3_t	forward, right;
	vec3_t	end;
	vec3_t	dir;

	if (!self->enemy || !self->enemy->inuse) // PGM
		return;								 // PGM

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[(self->s.frame & 1) ? MZ2_HOVER_BLASTER_2 : MZ2_HOVER_BLASTER_1], forward, right, start);

	VectorCopy (self->enemy->s.origin, end);
	end[2] += self->enemy->viewheight;
	VectorSubtract (end, start, dir);
	VectorNormalize (dir);

	// PGM	- daedalus fires blaster2
	if (self->mass < 200)
		monster_fire_blaster (self, start, dir, 1, 1000, (self->s.frame & 1) ? MZ2_HOVER_BLASTER_2 : MZ2_HOVER_BLASTER_1, (self->s.frame % 4) ? 0 : EF_HYPERBLASTER);
	else
		monster_fire_blaster2 (self, start, dir, 1, 1000, (self->s.frame & 1) ? MZ2_DAEDALUS_BLASTER_2 : MZ2_DAEDALUS_BLASTER, (self->s.frame % 4) ? 0 : EF_BLASTER);
	// PGM
}

static void hover_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &hover_move_stand;
}

static void hover_run (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &hover_move_stand;
	else
		self->monsterinfo.currentmove = &hover_move_run;
}

static void hover_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &hover_move_walk;
}

static void hover_start_attack (edict_t *self)
{
	self->monsterinfo.currentmove = &hover_move_start_attack;
}

static void hover_attack (edict_t *self)
{
	float	chance = 0.5f;

	if (self->mass > 150) // the daedalus strafes more
		chance += 0.1f;

	if (random() > chance)
	{
		self->monsterinfo.currentmove = &hover_move_attack1;
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}
	else // circle strafe
	{
		if (random() <= 0.5f) // switch directions
			self->monsterinfo.lefty = !self->monsterinfo.lefty;
		self->monsterinfo.currentmove = &hover_move_attack2;
		self->monsterinfo.attack_state = AS_SLIDING;
	}
}

static void hover_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	float	r;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	r = random();

	//====
	if (r < 0.5f)
	{
		// PMM - daedalus sounds
		if (self->mass < 225)
			gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
		else
			gi.sound (self, CHAN_VOICE, daed_sound_pain1, 1, ATTN_NORM, 0);
	}
	else
	{
		// PMM - daedalus sounds
		if (self->mass < 225)
			gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
		else
			gi.sound (self, CHAN_VOICE, daed_sound_pain2, 1, ATTN_NORM, 0);
	}
	// PGM
	//====

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	r = random();

	if (damage <= 25)
	{
		if (r < 0.5f)
			self->monsterinfo.currentmove = &hover_move_pain3;
		else
			self->monsterinfo.currentmove = &hover_move_pain2;
	}
	else
	{
		//====
		// PGM pain sequence is WAY too long
		if (r < 0.3f)
			self->monsterinfo.currentmove = &hover_move_pain1;
		else
			self->monsterinfo.currentmove = &hover_move_pain2;
		// PGM
		//====
	}
}

static void hover_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum |= 1; // PGM support for skins 2 & 3.
	else
		self->s.skinnum &= ~1; // PGM support for skins 2 & 3.
}

static void hover_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	self->movetype = MOVETYPE_TOSS;
	self->think = hover_deadthink;
	self->nextthink = level.time + FRAMETIME;
	self->timestamp = level.time + 15;
	gi.linkentity (self);
}

static void hover_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	self->s.effects = 0;
	self->monsterinfo.power_armor_type = POWER_ARMOR_NONE;

	if (M_CheckGib (self))
	{
		hover_gib (self);
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	// PMM - daedalus sounds
	if (self->mass < 225)
	{
		if (random() < 0.5f)
			gi.sound (self, CHAN_VOICE, sound_death1, 1, ATTN_NORM, 0);
		else
			gi.sound (self, CHAN_VOICE, sound_death2, 1, ATTN_NORM, 0);
	}
	else
	{
		if (random() < 0.5f)
			gi.sound (self, CHAN_VOICE, daed_sound_death1, 1, ATTN_NORM, 0);
		else
			gi.sound (self, CHAN_VOICE, daed_sound_death2, 1, ATTN_NORM, 0);
	}

	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
	self->monsterinfo.currentmove = &hover_move_death1;
}

static void hover_set_fly_parameters (edict_t *self)
{
	self->monsterinfo.fly_thrusters = false;
	self->monsterinfo.fly_acceleration = 20.f;
	self->monsterinfo.fly_speed = 120.f;
	// Icarus prefers to keep its distance, but flies slower than the flyer.
	// he never pins because of this.
	self->monsterinfo.fly_min_distance = 150.f;
	self->monsterinfo.fly_max_distance = 350.f;
}

void SP_monster_hover_rr (edict_t *self)
{
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/hover/tris.md2");

	gi.modelindex ("models/monsters/hover/gibs/chest.md2");
	gi.modelindex ("models/monsters/hover/gibs/foot.md2");
	gi.modelindex ("models/monsters/hover/gibs/head.md2");
	gi.modelindex ("models/monsters/hover/gibs/ring.md2");

	VectorSet (self->mins, -24, -24, -24);
	VectorSet (self->maxs, 24, 24, 32);

	self->health = 240;
	self->gib_health = -100;
	self->mass = 150;

	self->pain = hover_pain;
	self->die = hover_die;

	self->monsterinfo.stand = hover_stand;
	self->monsterinfo.walk = hover_walk;
	self->monsterinfo.run = hover_run;
	self->monsterinfo.attack = hover_start_attack;
	self->monsterinfo.sight = hover_sight;
	self->monsterinfo.search = hover_search;
	self->monsterinfo.setskin = hover_setskin;

	// PGM
	if (strcmp (self->classname, "monster_daedalus") == 0)
	{
		self->health = 450;
		self->mass = 225;
		self->yaw_speed = 23;
		self->monsterinfo.power_armor_type = POWER_ARMOR_SCREEN;
		self->monsterinfo.power_armor_power = 100;
		// PMM - daedalus sounds
		self->s.sound = gi.soundindex ("daedalus/daedidle1.wav");
		daed_sound_pain1 = gi.soundindex ("daedalus/daedpain1.wav");
		daed_sound_pain2 = gi.soundindex ("daedalus/daedpain2.wav");
		daed_sound_death1 = gi.soundindex ("daedalus/daeddeth1.wav");
		daed_sound_death2 = gi.soundindex ("daedalus/daeddeth2.wav");
		daed_sound_sight = gi.soundindex ("daedalus/daedsght1.wav");
		daed_sound_search1 = gi.soundindex ("daedalus/daedsrch1.wav");
		daed_sound_search2 = gi.soundindex ("daedalus/daedsrch2.wav");
		gi.soundindex ("tank/tnkatck3.wav");
		// pmm
	}
	else
	{
		self->yaw_speed = 18;
		sound_pain1 = gi.soundindex ("hover/hovpain1.wav");
		sound_pain2 = gi.soundindex ("hover/hovpain2.wav");
		sound_death1 = gi.soundindex ("hover/hovdeth1.wav");
		sound_death2 = gi.soundindex ("hover/hovdeth2.wav");
		sound_sight = gi.soundindex ("hover/hovsght1.wav");
		sound_search1 = gi.soundindex ("hover/hovsrch1.wav");
		sound_search2 = gi.soundindex ("hover/hovsrch2.wav");
		gi.soundindex ("hover/hovatck1.wav");
		self->s.sound = gi.soundindex ("hover/hovidle1.wav");
	}
	// PGM

	gi.linkentity (self);

	self->monsterinfo.currentmove = &hover_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	flymonster_start (self);

	// PGM
	if (strcmp (self->classname, "monster_daedalus") == 0)
		self->s.skinnum = 2;
	// PGM

	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	hover_set_fly_parameters (self);
}
