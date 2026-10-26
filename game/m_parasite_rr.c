// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

PARASITE, the 2023 re-release's

Its m_parasite.cpp, in C, for its own maps (RR_Map): a parasite that
shoots its proboscis at you as a thing of its own, aimed where you are
going, which flies, sticks to you and drains you while nothing comes
between you, and is reeled back in; that misses into a wall, sticks and
wrenches itself free; and that jumps ledges after you and gibs into
pieces of its own. Its frames are its own (m_parasite_rr.h), with its
jumps after 1997's. m_parasite.c is 1997's, for every other map.

What it does at 40 Hz is done at this game's 10.
==============================================================================
*/

#include "g_local.h"
#include "m_parasite_rr.h"

qboolean blocked_checkplat (edict_t *self, float dist);

#define PROBOSCIS_MISS_CHANCE		0.1f
#define PROBOSCIS_SPEED				1250
#define PROBOSCIS_RETRACT_MODIFIER	2.0f

static int	sound_pain1;
static int	sound_pain2;
static int	sound_die;
static int	sound_launch;
static int	sound_impact;
static int	sound_suck;
static int	sound_reelin;
static int	sound_sight;
static int	sound_tap;
static int	sound_scratch;
static int	sound_search;

static void parasite_stand (edict_t *self);
static void parasite_start_run (edict_t *self);
static void parasite_run (edict_t *self);
static void parasite_walk (edict_t *self);
static void parasite_do_fidget (edict_t *self);
static void parasite_refidget (edict_t *self);
static void parasite_dead (edict_t *self);
static void parasite_shrink (edict_t *self);
static void proboscis_retract (edict_t *self);
static void proboscis_segment_draw (edict_t *self);
static mmove_t parasite_move_fire_proboscis;

static void parasite_launch (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_launch, 1, ATTN_NORM, 0);
}

static void parasite_reel_in (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_reelin, 1, ATTN_NORM, 0);
}

static void parasite_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_WEAPON, sound_sight, 1, ATTN_NORM, 0);
}

static void parasite_tap (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_tap, 0.75f, 2.75f, 0);
}

static void parasite_scratch (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, sound_scratch, 0.75f, 2.75f, 0);
}

static mframe_t parasite_frames_start_fidget[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
static mmove_t parasite_move_start_fidget = { FRAME_stand18, FRAME_stand21, parasite_frames_start_fidget, parasite_do_fidget };

static mframe_t parasite_frames_fidget[] = {
	{ ai_stand, 0, parasite_scratch },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, parasite_scratch },
	{ ai_stand },
	{ ai_stand }
};
static mmove_t parasite_move_fidget = { FRAME_stand22, FRAME_stand27, parasite_frames_fidget, parasite_refidget };

static mframe_t parasite_frames_end_fidget[] = {
	{ ai_stand, 0, parasite_scratch },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand }
};
static mmove_t parasite_move_end_fidget = { FRAME_stand28, FRAME_stand35, parasite_frames_end_fidget, parasite_stand };

static void parasite_do_fidget (edict_t *self)
{
	self->monsterinfo.currentmove = &parasite_move_fidget;
}

static void parasite_refidget (edict_t *self)
{
	if (random() <= 0.8f)
		self->monsterinfo.currentmove = &parasite_move_fidget;
	else
		self->monsterinfo.currentmove = &parasite_move_end_fidget;
}

static void parasite_idle (edict_t *self)
{
	if (self->enemy)
		return;

	self->monsterinfo.currentmove = &parasite_move_start_fidget;
}

static mframe_t parasite_frames_stand[] = {
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, parasite_tap },
	{ ai_stand },
	{ ai_stand, 0, parasite_tap },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, parasite_tap },
	{ ai_stand },
	{ ai_stand, 0, parasite_tap },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand },
	{ ai_stand, 0, parasite_tap },
	{ ai_stand },
	{ ai_stand, 0, parasite_tap }
};
static mmove_t parasite_move_stand = { FRAME_stand01, FRAME_stand17, parasite_frames_stand, parasite_stand };

static void parasite_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &parasite_move_stand;
}

static mframe_t parasite_frames_run[] = {
	{ ai_run, 30 },
	{ ai_run, 30 },
	{ ai_run, 22, monster_footstep },
	{ ai_run, 19, monster_footstep },
	{ ai_run, 24 },
	{ ai_run, 28, monster_footstep },
	{ ai_run, 25, monster_footstep }
};
static mmove_t parasite_move_run = { FRAME_run03, FRAME_run09, parasite_frames_run, NULL };

static mframe_t parasite_frames_start_run[] = {
	{ ai_run },
	{ ai_run, 30 },
};
static mmove_t parasite_move_start_run = { FRAME_run01, FRAME_run02, parasite_frames_start_run, parasite_run };

static void parasite_start_run (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &parasite_move_stand;
	else
		self->monsterinfo.currentmove = &parasite_move_start_run;
}

static void parasite_run (edict_t *self)
{
	if (self->proboscus && self->proboscus->style != 2)
		proboscis_retract (self->proboscus);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &parasite_move_stand;
	else
		self->monsterinfo.currentmove = &parasite_move_run;
}

static mframe_t parasite_frames_walk[] = {
	{ ai_walk, 30 },
	{ ai_walk, 30 },
	{ ai_walk, 22, monster_footstep },
	{ ai_walk, 19, monster_footstep },
	{ ai_walk, 24 },
	{ ai_walk, 28, monster_footstep },
	{ ai_walk, 25, monster_footstep }
};
static mmove_t parasite_move_walk = { FRAME_run03, FRAME_run09, parasite_frames_walk, parasite_walk };

static mframe_t parasite_frames_start_walk[] = {
	{ ai_walk, 0 },
	{ ai_walk, 30, parasite_walk }
};
static mmove_t parasite_move_start_walk = { FRAME_run01, FRAME_run02, parasite_frames_start_walk, NULL };

static void parasite_start_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &parasite_move_start_walk;
}

static void parasite_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &parasite_move_walk;
}

// hard reset on proboscis; like we never existed
static void proboscis_reset (edict_t *self)
{
	self->owner->proboscus = NULL;
	G_FreeEdict (self->proboscus);
	G_FreeEdict (self);
}

static void proboscis_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (meansOfDeath == MOD_CRUSH)
		proboscis_reset (self);
}

static void parasite_break_wait (edict_t *self)
{
	// prob exploded?
	if (self->proboscus && self->proboscus->style != 3)
		self->monsterinfo.nextframe = FRAME_break19;
	else if (rand () & 1)
	{
		// don't get hurt
		parasite_reel_in (self);
		self->monsterinfo.nextframe = FRAME_break31;
	}
}

static void proboscis_retract (edict_t *self)
{
	// start retract animation
	if (self->owner->monsterinfo.currentmove == &parasite_move_fire_proboscis)
		self->owner->monsterinfo.nextframe = FRAME_drain12;

	// mark as retracting
	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_NOT;
	// come back real hard
	if (self->style != 2)
		self->speed *= PROBOSCIS_RETRACT_MODIFIER;
	self->style = 2;
	gi.linkentity (self);
}

static void parasite_break_retract (edict_t *self)
{
	if (self->proboscus)
		proboscis_retract (self->proboscus);
}

static void parasite_break_sound (edict_t *self)
{
	if (random() < 0.5f)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	self->pain_debounce_time = level.time + 3;
}

static void parasite_charge_proboscis (edict_t *self, float dist)
{
	if (self->s.frame >= FRAME_break01 && self->s.frame <= FRAME_break32)
		ai_move (self, dist);
	else
		ai_charge (self, dist);

	if (self->proboscus)
		proboscis_segment_draw (self->proboscus->proboscus);
}

static void parasite_break_noise (edict_t *self)
{
	gi.sound (self, CHAN_VOICE, sound_search, 1, ATTN_NORM, 0);
}

static mframe_t parasite_frames_break[] = {
	{ parasite_charge_proboscis },
	{ parasite_charge_proboscis, -3, parasite_break_noise },
	{ parasite_charge_proboscis, 1 },
	{ parasite_charge_proboscis, 2 },
	{ parasite_charge_proboscis, -3 },
	{ parasite_charge_proboscis, 1 },
	{ parasite_charge_proboscis, 1 },
	{ parasite_charge_proboscis, 3 },
	{ parasite_charge_proboscis, 0, parasite_break_noise },
	{ parasite_charge_proboscis, -18 },
	{ parasite_charge_proboscis, 3 },
	{ parasite_charge_proboscis, 9 },
	{ parasite_charge_proboscis, 6 },
	{ parasite_charge_proboscis },
	{ parasite_charge_proboscis, -18 },
	{ parasite_charge_proboscis },
	{ parasite_charge_proboscis, 8, parasite_break_retract },
	{ parasite_charge_proboscis, 9 },
	{ parasite_charge_proboscis, 0, parasite_break_wait },
	{ parasite_charge_proboscis, -18, parasite_break_sound },
	{ parasite_charge_proboscis },
	{ parasite_charge_proboscis }, // airborne
	{ parasite_charge_proboscis }, // airborne
	{ parasite_charge_proboscis }, // slides
	{ parasite_charge_proboscis }, // slides
	{ parasite_charge_proboscis }, // slides
	{ parasite_charge_proboscis }, // slides
	{ parasite_charge_proboscis, 4 },
	{ parasite_charge_proboscis, 11 },
	{ parasite_charge_proboscis, -2 },
	{ parasite_charge_proboscis, -5 },
	{ parasite_charge_proboscis, 1 }
};
static mmove_t parasite_move_break = { FRAME_break01, FRAME_break32, parasite_frames_break, parasite_start_run };

// where it hit: the end of its move, against the plane it hit
static void proboscis_hit (edict_t *self, edict_t *other, vec3_t normal)
{
	vec3_t	p, endpos;

	// owner isn't trying to probe any more, don't touch anything
	if (self->owner->monsterinfo.currentmove != &parasite_move_fire_proboscis)
		return;

	VectorCopy (self->s.origin, endpos);

	// hit what we want to succ
	if (other->client || other == self->owner->enemy)
	{
		// into them, as its move leaves it half a unit off them
		VectorMA (endpos, -12, normal, p);

		self->owner->monsterinfo.nextframe = FRAME_drain06;
		self->movetype = MOVETYPE_NONE;
		self->solid = SOLID_NOT;
		self->style = 1;
		// stick to this guy
		VectorSubtract (p, other->s.origin, self->move_origin);
		self->enemy = other;
		// its alpha of .35, as this has none
		self->s.renderfx |= RF_TRANSLUCENT;
		gi.sound (self, CHAN_WEAPON, sound_suck, 1, ATTN_NORM, 0);
	}
	else
	{
		VectorAdd (endpos, normal, p);

		// hit monster, don't suck but do small damage
		// and retract immediately
		if (other->svflags & (SVF_MONSTER | SVF_DEADMONSTER))
			proboscis_retract (self);
		else
		{
			// hit wall; stick to it and do break animation
			self->owner->monsterinfo.currentmove = &parasite_move_break;
			self->movetype = MOVETYPE_NONE;
			self->solid = SOLID_NOT;
			self->style = 1;
			self->owner->s.angles[YAW] = self->s.angles[YAW];
		}
	}

	if (other->takedamage)
		T_Damage (other, self, self->owner, normal, endpos, normal, 5, 0, 0, MOD_UNKNOWN);

	gi.positioned_sound (endpos, self->owner, CHAN_AUTO, sound_impact, 1, ATTN_NORM, 0);
	VectorCopy (p, self->s.origin);
	self->nextthink = level.time + FRAMETIME; // start doing stuff on next frame
	gi.linkentity (self);
}

static void proboscis_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	normal;

	if (plane)
		VectorCopy (plane->normal, normal);
	else
	{
		VectorCopy (self->velocity, normal);
		VectorNormalize (normal);
		VectorNegate (normal, normal);
	}
	proboscis_hit (self, other, normal);
}

// from break01
static vec3_t parasite_break_offsets[] = {
	{ 7.0f, 0, 7.0f },
	{ 6.3f, 14.5f, 4.0f },
	{ 8.5f, 0, 5.6f },
	{ 5.0f, -15.25f, 4.0f, },
	{ 9.5f, -1.8f, 5.9f },
	{ 6.2f, 14.f, 4.0f },
	{ 12.25f, 7.5f, 1.4f },
	{ 13.8f, 0, -2.4f },
	{ 13.8f, 0, -4.0f },
	{ 0.1f, 0, -0.7f },
	{ 5.0f, 0, 3.7f },
	{ 11.f, 0, 4.f },
	{ 13.5f, 0, -4.0f },
	{ 13.5f, 0, -4.0f },
	{ 0.2f, 0, -0.7f },
	{ 3.9f, 0, 3.6f },
	{ 8.5f, 0, 5.0f },
	{ 14.0f, 0, -4.f },
	{ 14.0f, 0, -4.f },
	{ 0.1f, 0, -0.5f }
};

// from drain01
static vec3_t parasite_drain_offsets[] = {
	{ -1.7f, 0, 1.2f },
	{ -2.2f, 0, -0.6f },
	{ 7.7f, 0, 7.2f },
	{ 7.2f, 0, 5.7f },
	{ 6.2f, 0, 7.8f },
	{ 4.7f, 0, 6.7f },
	{ 5.0f, 0, 9.0f },
	{ 5.0f, 0, 7.0f },
	{ 5.0f, 0, 10.5f },
	{ 4.5f, 0, 9.7f },
	{ 1.5f, 0, 12.0f },
	{ 2.9f, 0, 11.0f },
	{ 2.1f, 0, 7.6f },
};

static void parasite_get_proboscis_start (edict_t *self, vec3_t start)
{
	vec3_t	f, r;
	vec3_t	offset = { 8, 0, 6 };
	int		n;

	AngleVectors (self->s.angles, f, r, NULL);

	n = self->s.frame - FRAME_break01;
	if (n >= 0 && n < sizeof(parasite_break_offsets) / sizeof(parasite_break_offsets[0]))
		VectorCopy (parasite_break_offsets[n], offset);
	n = self->s.frame - FRAME_drain01;
	if (n >= 0 && n < sizeof(parasite_drain_offsets) / sizeof(parasite_drain_offsets[0]))
		VectorCopy (parasite_drain_offsets[n], offset);

	G_ProjectSource (self->s.origin, offset, f, r, start);
}

static void proboscis_think (edict_t *self)
{
	vec3_t	start, dir, to_target, from_owner;
	float	dist;
	trace_t	tr;

	self->nextthink = level.time + FRAMETIME; // start doing stuff on next frame

	// retracting; keep pulling until we hit the parasite
	if (self->style == 2)
	{
		parasite_get_proboscis_start (self->owner, start);
		VectorSubtract (self->s.origin, start, dir);
		dist = VectorNormalize (dir);

		if (dist <= (self->speed * 2) * FRAMETIME)
		{
			// reached target; free self on next frame, let parasite know
			self->style = 3;
			self->think = proboscis_reset;
			VectorCopy (start, self->s.origin);
			gi.linkentity (self);
			return;
		}

		// pull us in
		VectorMA (self->s.origin, -self->speed * FRAMETIME, dir, self->s.origin);
		gi.linkentity (self);
	}
	// stuck on target; do damage, suck health
	// and check if target goes away
	else if (self->style == 1)
	{
		if (!self->enemy)
		{
			// stuck in wall
		}
		else if (!self->enemy->inuse || self->enemy->health <= 0 || !self->enemy->takedamage)
		{
			// target gone, retract early
			proboscis_retract (self);
		}
		else
		{
			// update our position
			VectorAdd (self->enemy->s.origin, self->move_origin, self->s.origin);

			parasite_get_proboscis_start (self->owner, start);
			VectorSubtract (self->s.origin, start, dir);
			vectoangles (dir, self->s.angles);

			// see if we got cut by the world
			tr = gi.trace (start, NULL, NULL, self->s.origin, NULL, MASK_SOLID);

			if (tr.fraction != 1.0f)
			{
				// blocked, so retract
				proboscis_retract (self);
				VectorCopy (self->s.old_origin, self->s.origin);
			}
			else
			{
				// succ & drain
				if (self->timestamp <= level.time)
				{
					T_Damage (self->enemy, self, self->owner, tr.plane.normal, tr.endpos, tr.plane.normal, 2, 0, 0, MOD_UNKNOWN);
					self->owner->health += 2;
					if (self->owner->health > self->owner->max_health)
						self->owner->health = self->owner->max_health;
					self->owner->monsterinfo.setskin (self->owner);
					self->timestamp = level.time + 0.1f;
				}
			}

			gi.linkentity (self);
		}
	}
	// flying
	else if (self->style == 0)
	{
		// owner gone away?
		if (!self->owner->enemy || !self->owner->enemy->inuse || self->owner->enemy->health <= 0)
		{
			proboscis_retract (self);
			return;
		}

		// if we're well behind our target and missed by 2x velocity,
		// be smart enough to pull in automatically
		VectorSubtract (self->s.origin, self->owner->enemy->s.origin, to_target);
		dist = VectorNormalize (to_target);

		if (dist > (self->speed * 2) / 15.f)
		{
			VectorSubtract (self->s.origin, self->owner->s.origin, from_owner);
			VectorNormalize (from_owner);

			if (DotProduct (to_target, from_owner) > 0.f)
			{
				proboscis_retract (self);
				return;
			}
		}
	}
}

// the segment, drawn as a model beam from the parasite to the tip
static void proboscis_segment_draw (edict_t *self)
{
	vec3_t	start, dir;

	parasite_get_proboscis_start (self->owner->owner, start);

	VectorCopy (start, self->s.origin);
	VectorSubtract (self->owner->s.origin, start, dir);
	VectorNormalize (dir);
	VectorMA (self->owner->s.origin, -8.f, dir, self->s.old_origin);
	gi.linkentity (self);
}

static void fire_proboscis (edict_t *self, vec3_t start, vec3_t dir, float speed)
{
	edict_t	*tip, *segment;
	vec3_t	end;
	trace_t	tr;

	tip = G_Spawn ();
	vectoangles (dir, tip->s.angles);
	tip->s.modelindex = gi.modelindex ("models/monsters/parasite/tip/tris.md2");
	tip->movetype = MOVETYPE_FLYMISSILE;
	tip->owner = self;
	self->proboscus = tip;
	tip->clipmask = MASK_SHOT & ~CONTENTS_DEADMONSTER;
	VectorCopy (start, tip->s.origin);
	VectorCopy (start, tip->s.old_origin);
	tip->speed = speed;
	VectorScale (dir, speed, tip->velocity);
	tip->solid = SOLID_BBOX;
	tip->takedamage = DAMAGE_YES;
	tip->flags |= FL_NO_KNOCKBACK;
	tip->die = proboscis_die;
	tip->touch = proboscis_touch;
	tip->think = proboscis_think;
	tip->nextthink = level.time + FRAMETIME; // start doing stuff on next frame
	tip->classname = "proboscis";

	segment = G_Spawn ();
	segment->s.modelindex = gi.modelindex ("models/monsters/parasite/segment/tris.md2");
	segment->s.renderfx = RF_BEAM;
	segment->postthink = proboscis_segment_draw;
	segment->classname = "proboscis";

	tip->proboscus = segment;
	segment->owner = tip;

	VectorMA (tip->s.origin, FRAMETIME, tip->velocity, end);
	tr = gi.trace (tip->s.origin, NULL, NULL, end, self, tip->clipmask);
	if (tr.startsolid)
	{
		VectorNegate (dir, tr.plane.normal);
		tip->touch (tip, tr.ent, &tr.plane, tr.surface);
	}
	else if (tr.fraction < 1.0f)
	{
		// at what it hit, as the touch of its move would be
		VectorCopy (tr.endpos, tip->s.origin);
		tip->touch (tip, tr.ent, &tr.plane, tr.surface);
	}

	VectorCopy (start, segment->s.origin);
	VectorSubtract (tip->s.origin, start, end);
	VectorNormalize (end);
	VectorMA (tip->s.origin, 8.f, end, segment->s.old_origin);

	gi.linkentity (tip);
	gi.linkentity (segment);
}

static void parasite_fire_proboscis (edict_t *self)
{
	vec3_t	start, dir;

	if (self->proboscus && self->proboscus->style != 2)
		proboscis_reset (self->proboscus);

	parasite_get_proboscis_start (self, start);
	PredictAim (self, self->enemy, start, PROBOSCIS_SPEED, false, crandom() * PROBOSCIS_MISS_CHANCE, dir, NULL);

	fire_proboscis (self, start, dir, PROBOSCIS_SPEED);
}

static void parasite_proboscis_wait (edict_t *self)
{
	// loop frames while we wait
	if (self->s.frame == FRAME_drain04)
		self->monsterinfo.nextframe = FRAME_drain05;
	else
		self->monsterinfo.nextframe = FRAME_drain04;
}

static void parasite_proboscis_pull_wait (edict_t *self)
{
	// prob exploded?
	if (!self->proboscus || self->proboscus->style == 3)
	{
		self->monsterinfo.nextframe = FRAME_drain14;
		return;
	}

	// being pulled in, so wait until we get destroyed
	if (self->s.frame == FRAME_drain12)
		self->monsterinfo.nextframe = FRAME_drain13;
	else
		self->monsterinfo.nextframe = FRAME_drain12;

	if (self->proboscus->style != 2)
		proboscis_retract (self->proboscus);
}

static mframe_t parasite_frames_fire_proboscis[] = {
	{ parasite_charge_proboscis, 0, parasite_launch },
	{ parasite_charge_proboscis },
	{ parasite_charge_proboscis, 15, parasite_fire_proboscis }, // Target hits
	{ parasite_charge_proboscis, 0, parasite_proboscis_wait },  // drain
	{ parasite_charge_proboscis, 0, parasite_proboscis_wait },  // drain
	{ parasite_charge_proboscis, 0 },  // drain
	{ parasite_charge_proboscis, 0 },  // drain
	{ parasite_charge_proboscis, -2 }, // drain
	{ parasite_charge_proboscis, -2 }, // drain
	{ parasite_charge_proboscis, -3 }, // drain
	{ parasite_charge_proboscis, -2 }, // drain
	{ parasite_charge_proboscis, 0, parasite_proboscis_pull_wait },  // drain
	{ parasite_charge_proboscis, -1, parasite_proboscis_pull_wait }, // drain
	{ parasite_charge_proboscis, 0, parasite_reel_in },		  // let go
	{ parasite_charge_proboscis, -2 },
	{ parasite_charge_proboscis, -2 },
	{ parasite_charge_proboscis, -3 },
	{ parasite_charge_proboscis }
};
static mmove_t parasite_move_fire_proboscis = { FRAME_drain01, FRAME_drain18, parasite_frames_fire_proboscis, parasite_start_run };

static void parasite_attack (edict_t *self)
{
	vec3_t	clear;

	if (!M_CheckClearShot (self, parasite_drain_offsets[0], clear))
		return;

	if (self->proboscus && self->proboscus->style != 2)
		proboscis_retract (self->proboscus);

	self->monsterinfo.currentmove = &parasite_move_fire_proboscis;
}

//================
// ROGUE
static void parasite_jump_down (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 100, forward, self->velocity);
	VectorMA (self->velocity, 300, up, self->velocity);
}

static void parasite_jump_up (edict_t *self)
{
	vec3_t	forward, up;

	AngleVectors (self->s.angles, forward, NULL, up);
	VectorMA (self->velocity, 200, forward, self->velocity);
	VectorMA (self->velocity, 450, up, self->velocity);
}

static void parasite_jump_wait_land (edict_t *self)
{
	if (self->groundentity == NULL)
	{
		self->monsterinfo.nextframe = self->s.frame;

		if (monster_jump_finished_rr (self))
			self->monsterinfo.nextframe = self->s.frame + 1;
	}
	else
		self->monsterinfo.nextframe = self->s.frame + 1;
}

static mframe_t parasite_frames_jump_up[] = {
	{ ai_move, -8 },
	{ ai_move, -8 },
	{ ai_move, -8 },
	{ ai_move, -8, parasite_jump_up },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, parasite_jump_wait_land },
	{ ai_move }
};
static mmove_t parasite_move_jump_up = { FRAME_jump01, FRAME_jump08, parasite_frames_jump_up, parasite_run };

static mframe_t parasite_frames_jump_down[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, parasite_jump_down },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, parasite_jump_wait_land },
	{ ai_move }
};
static mmove_t parasite_move_jump_down = { FRAME_jump01, FRAME_jump08, parasite_frames_jump_down, parasite_run };

static void parasite_jump (edict_t *self, int result)
{
	if (!self->enemy)
		return;

	if (result == JUMP_UP)
		self->monsterinfo.currentmove = &parasite_move_jump_up;
	else
		self->monsterinfo.currentmove = &parasite_move_jump_down;
}

static qboolean parasite_blocked (edict_t *self, float dist)
{
	int		result;

	if ((result = blocked_checkjump_rr (self, dist)) != JUMP_NO)
	{
		if (result != JUMP_TURN)
			parasite_jump (self, result);
		return true;
	}

	return blocked_checkplat (self, dist);
}
// ROGUE
//================

static void parasite_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	monster_dead (self);
}

static void parasite_shrink (edict_t *self)
{
	self->maxs[2] = 0;
	self->svflags |= SVF_DEADMONSTER;
	gi.linkentity (self);
}

static mframe_t parasite_frames_death[] = {
	{ ai_move, 0, NULL },
	{ ai_move },
	{ ai_move },
	{ ai_move, 0, parasite_shrink },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move }
};
static mmove_t parasite_move_death = { FRAME_death101, FRAME_death107, parasite_frames_death, parasite_dead };

static gib_def_t parasite_gibs[] = {
	{ 1, "models/objects/gibs/bone/tris.md2" },
	{ 3, "models/objects/gibs/sm_meat/tris.md2" },
	{ 1, "models/monsters/parasite/gibs/chest.md2", GIB_SKINNED },
	{ 2, "models/monsters/parasite/gibs/bleg.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 2, "models/monsters/parasite/gibs/fleg.md2", GIB_SKINNED | GIB_UPRIGHT },
	{ 1, "models/monsters/parasite/gibs/head.md2", GIB_SKINNED | GIB_HEAD },
	{ 0, NULL }
};

static void parasite_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (self->proboscus && self->proboscus->style != 2)
		proboscis_reset (self->proboscus);

	// check for gib
	if (M_CheckGib (self))
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);

		self->s.skinnum /= 2;

		ThrowGibs (self, damage, parasite_gibs);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	gi.sound (self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
	self->monsterinfo.currentmove = &parasite_move_death;
}

static void parasite_pain_skip (edict_t *self)
{
	self->monsterinfo.nextframe = FRAME_pain105;
}

static mframe_t parasite_frames_pain1[] = {
	{ ai_move, 0, NULL },
	{ ai_move },
	{ ai_move, 0, parasite_pain_skip },
	{ ai_move, 0, monster_footstep },
	{ ai_move },
	{ ai_move },
	{ ai_move, 6, monster_footstep },
	{ ai_move, 16 },
	{ ai_move, -6, monster_footstep },
	{ ai_move, -7 },
	{ ai_move }
};
static mmove_t parasite_move_pain1 = { FRAME_pain101, FRAME_pain111, parasite_frames_pain1, parasite_start_run };

static void parasite_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (level.time < self->pain_debounce_time)
		return;

	if (self->proboscus && self->proboscus->style != 2)
		proboscis_retract (self->proboscus);

	self->pain_debounce_time = level.time + 3;

	if (random() < 0.5f)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	if (!M_ShouldReactToPain (self))
		return; // no pain anims in nightmare

	self->monsterinfo.currentmove = &parasite_move_pain1;
}

static void parasite_setskin (edict_t *self)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;
	else
		self->s.skinnum = 0;
}

#define SPAWNFLAG_PARASITE_NOJUMPING	8

void SP_monster_parasite_rr (edict_t *self)
{
	sound_pain1 = gi.soundindex ("parasite/parpain1.wav");
	sound_pain2 = gi.soundindex ("parasite/parpain2.wav");
	sound_die = gi.soundindex ("parasite/pardeth1.wav");
	sound_launch = gi.soundindex ("parasite/paratck1.wav");
	sound_impact = gi.soundindex ("parasite/paratck2.wav");
	sound_suck = gi.soundindex ("parasite/paratck3.wav");
	sound_reelin = gi.soundindex ("parasite/paratck4.wav");
	sound_sight = gi.soundindex ("parasite/parsght1.wav");
	sound_tap = gi.soundindex ("parasite/paridle1.wav");
	sound_scratch = gi.soundindex ("parasite/paridle2.wav");
	sound_search = gi.soundindex ("parasite/parsrch1.wav");

	gi.modelindex ("models/monsters/parasite/tip/tris.md2");
	gi.modelindex ("models/monsters/parasite/segment/tris.md2");
	self->s.modelindex = gi.modelindex ("models/monsters/parasite/tris.md2");

	gi.modelindex ("models/monsters/parasite/gibs/head.md2");
	gi.modelindex ("models/monsters/parasite/gibs/chest.md2");
	gi.modelindex ("models/monsters/parasite/gibs/bleg.md2");
	gi.modelindex ("models/monsters/parasite/gibs/fleg.md2");

	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, 24);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	self->health = 175;
	self->gib_health = -50;
	self->mass = 250;

	self->pain = parasite_pain;
	self->die = parasite_die;

	self->monsterinfo.stand = parasite_stand;
	self->monsterinfo.walk = parasite_start_walk;
	self->monsterinfo.run = parasite_start_run;
	self->monsterinfo.attack = parasite_attack;
	self->monsterinfo.sight = parasite_sight;
	self->monsterinfo.idle = parasite_idle;
	self->monsterinfo.blocked = parasite_blocked; // PGM
	self->monsterinfo.setskin = parasite_setskin;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &parasite_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;
	self->yaw_speed = 30;
	self->monsterinfo.can_jump = !(self->spawnflags & SPAWNFLAG_PARASITE_NOJUMPING);
	self->monsterinfo.drop_height = 256;
	self->monsterinfo.jump_height = 68;

	walkmonster_start (self);
}
