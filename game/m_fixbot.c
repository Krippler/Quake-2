/*
==============================================================================

The Reckoning's fixbot (monster_fixbot), from the re-release's
rerelease/xatrix/m_xatrix_fixbot.cpp: a small flyer that welds what
needs fixing (func_object_repair), lands and takes off where its map
says, raises the dead with its healing ray as a medic does, and charges
in firing its blaster otherwise. This game has no other of it.

==============================================================================
*/

#include "g_local.h"
#include "m_fixbot.h"

#define SPAWNFLAG_FIXBOT_FIXIT		4
#define SPAWNFLAG_FIXBOT_TAKEOFF	8
#define SPAWNFLAG_FIXBOT_LANDING	16
#define SPAWNFLAG_FIXBOT_WORKING	32

qboolean infront (edict_t *self, edict_t *other);
qboolean FindTarget (edict_t *self);
void ED_CallSpawn (edict_t *ent);
void abortHeal (edict_t *self, qboolean change_frame, qboolean gib, qboolean mark);
qboolean M_CorpseAtRest (edict_t *ent);

static int	sound_pain1;
static int	sound_die;
static int	sound_weld1;
static int	sound_weld2;
static int	sound_weld3;

static void fixbot_run (edict_t *self);
static void fixbot_attack (edict_t *self);
static void fixbot_fire_blaster (edict_t *self);
static void fixbot_fire_welder (edict_t *self);
static void fixbot_fire_laser (edict_t *self);
static void use_scanner (edict_t *self);
static void change_to_roam (edict_t *self);
static void fly_vertical (edict_t *self);
static void fly_vertical2 (edict_t *self);
static void fixbot_stand (edict_t *self);
static void roam_goal (edict_t *self);
static void weldstate (edict_t *self);
static void ai_facing (edict_t *self, float dist);
static void ai_move2 (edict_t *self, float dist);
static void ai_movetogoal (edict_t *self, float dist);

static mframe_t fixbot_frames_landing[] = {
	{ ai_move },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },

	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 },
	{ ai_move, 0, fly_vertical2 }
};

static mframe_t fixbot_frames_stand[] = {
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
	{ ai_move, 0, change_to_roam }

};

static mframe_t fixbot_frames_stand2[] = {
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
	{ ai_stand, 0, change_to_roam }
};

static mframe_t fixbot_frames_roamgoal[] = {
	{ ai_move, 0, roam_goal }
};

static mframe_t fixbot_frames_turn[] = {
	{ ai_facing }
};

static mframe_t fixbot_frames_takeoff[] = {
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },

	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical },
	{ ai_move, 0.01f, fly_vertical }
};

static mframe_t fixbot_frames_paina[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};

static mframe_t fixbot_frames_painb[] = {
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move },
	{ ai_move }
};

static mframe_t fixbot_frames_pain3[] = {
	{ ai_move, -1 }
};

static mframe_t fixbot_frames_forward[] = {
	{ ai_movetogoal, 5, use_scanner }
};

static mframe_t fixbot_frames_walk[] = {
	{ ai_walk, 5 }
};

static mframe_t fixbot_frames_run[] = {
	{ ai_run, 10 }
};

static mframe_t fixbot_frames_start_attack[] = {
	{ ai_charge }
};

static mframe_t fixbot_frames_laserattack[] = {
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser },
	{ ai_charge, 0, fixbot_fire_laser }
};

static mframe_t fixbot_frames_attack2[] = {
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },

	{ ai_charge, -10 },
	{ ai_charge, -10 },
	{ ai_charge, -10 },
	{ ai_charge, -10 },
	{ ai_charge, -10 },
	{ ai_charge, -10 },
	{ ai_charge, -10 },
	{ ai_charge, -10 },
	{ ai_charge, -10 },
	{ ai_charge, -10 },

	{ ai_charge, 0, fixbot_fire_blaster },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge },

	{ ai_charge }
};

static mframe_t fixbot_frames_weld_start[] = {
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0 },
	{ ai_move2, 0, weldstate }
};

static mframe_t fixbot_frames_weld[] = {
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, fixbot_fire_welder },
	{ ai_move2, 0, weldstate }
};

static mframe_t fixbot_frames_weld_end[] = {
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2 },
	{ ai_move2, -2, weldstate }
};

static mmove_t fixbot_move_landing = { FRAME_landing_01, FRAME_landing_58, fixbot_frames_landing, NULL };
static mmove_t fixbot_move_stand = { FRAME_ambient_01, FRAME_ambient_19, fixbot_frames_stand, NULL };
static mmove_t fixbot_move_stand2 = { FRAME_ambient_01, FRAME_ambient_19, fixbot_frames_stand2, NULL };
static mmove_t fixbot_move_roamgoal = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_roamgoal, NULL };
static mmove_t fixbot_move_turn = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_turn, NULL };
static mmove_t fixbot_move_takeoff = { FRAME_takeoff_01, FRAME_takeoff_16, fixbot_frames_takeoff, NULL };
static mmove_t fixbot_move_paina = { FRAME_paina_01, FRAME_paina_06, fixbot_frames_paina, fixbot_run };
static mmove_t fixbot_move_painb = { FRAME_painb_01, FRAME_painb_08, fixbot_frames_painb, fixbot_run };
static mmove_t fixbot_move_pain3 = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_pain3, fixbot_run };
static mmove_t fixbot_move_forward = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_forward, NULL };
static mmove_t fixbot_move_walk = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_walk, NULL };
static mmove_t fixbot_move_run = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_run, NULL };
static mmove_t fixbot_move_start_attack = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_start_attack, fixbot_attack };
static mmove_t fixbot_move_laserattack = { FRAME_shoot_01, FRAME_shoot_06, fixbot_frames_laserattack, NULL };
static mmove_t fixbot_move_attack2 = { FRAME_charging_01, FRAME_charging_31, fixbot_frames_attack2, fixbot_run };
static mmove_t fixbot_move_weld_start = { FRAME_weldstart_01, FRAME_weldstart_10, fixbot_frames_weld_start, NULL };
static mmove_t fixbot_move_weld = { FRAME_weldmiddle_01, FRAME_weldmiddle_07, fixbot_frames_weld, NULL };
static mmove_t fixbot_move_weld_end = { FRAME_weldend_01, FRAME_weldend_07, fixbot_frames_weld_end, NULL };

static void bot_goal_check (edict_t *self)
{
	if (!self->owner || !self->owner->inuse || self->owner->goalentity != self)
	{
		G_FreeEdict (self);
		return;
	}

	self->nextthink = level.time + FRAMETIME;
}

// a dead monster about, the fittest it can see
static edict_t *fixbot_FindDeadMonster (edict_t *self)
{
	edict_t	*ent = NULL;
	edict_t	*best = NULL;

	while ((ent = findradius (ent, self->s.origin, 1024)) != NULL)
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
		if (!best)
		{
			best = ent;
			continue;
		}
		if (ent->max_health <= best->max_health)
			continue;
		best = ent;
	}

	return best;
}

static void fixbot_set_fly_parameters (edict_t *self, qboolean heal, qboolean weld)
{
	self->monsterinfo.fly_position_time = 0;
	self->monsterinfo.fly_acceleration = 5;
	self->monsterinfo.fly_speed = 110;
	self->monsterinfo.fly_buzzard = false;

	if (heal)
	{
		self->monsterinfo.fly_min_distance = 100;
		self->monsterinfo.fly_max_distance = 100;
		self->monsterinfo.fly_thrusters = true;
	}
	else if (weld)
	{
		self->monsterinfo.fly_min_distance = 24;
		self->monsterinfo.fly_max_distance = 24;
	}
	else
	{
		// timid bot
		self->monsterinfo.fly_min_distance = 300;
		self->monsterinfo.fly_max_distance = 500;
	}
}

static int fixbot_search (edict_t *self)
{
	edict_t *ent;

	if (!self->enemy)
	{
		ent = fixbot_FindDeadMonster (self);
		if (ent)
		{
			self->oldenemy = self->enemy;
			self->enemy = ent;
			self->enemy->monsterinfo.healer = self;
			self->monsterinfo.aiflags |= AI_MEDIC;
			FoundTarget (self);
			fixbot_set_fly_parameters (self, true, false);
			return 1;
		}
	}
	return 0;
}

// a goal for it to go to, which frees itself once the bot is done with it
static edict_t *fixbot_goal (edict_t *self)
{
	edict_t *ent;

	ent = G_Spawn ();
	ent->classname = "bot_goal";
	ent->solid = SOLID_BBOX;
	ent->owner = self;
	ent->think = bot_goal_check;
	gi.linkentity (ent);

	return ent;
}

static void landing_goal (edict_t *self)
{
	trace_t	tr;
	vec3_t	forward, right, up;
	vec3_t	end;
	edict_t	*ent;

	ent = fixbot_goal (self);
	VectorSet (ent->mins, -32, -32, -24);
	VectorSet (ent->maxs, 32, 32, 24);

	AngleVectors (self->s.angles, forward, right, up);
	VectorMA (self->s.origin, -8096, up, end);

	tr = gi.trace (self->s.origin, ent->mins, ent->maxs, end, self, MASK_MONSTERSOLID);

	VectorCopy (tr.endpos, ent->s.origin);

	self->goalentity = self->enemy = ent;
	self->monsterinfo.currentmove = &fixbot_move_landing;
}

static void takeoff_goal (edict_t *self)
{
	trace_t	tr;
	vec3_t	forward, right, up;
	vec3_t	end;
	edict_t	*ent;

	ent = fixbot_goal (self);
	VectorSet (ent->mins, -32, -32, -24);
	VectorSet (ent->maxs, 32, 32, 24);

	AngleVectors (self->s.angles, forward, right, up);
	VectorMA (self->s.origin, 128, up, end);

	tr = gi.trace (self->s.origin, ent->mins, ent->maxs, end, self, MASK_MONSTERSOLID);

	VectorCopy (tr.endpos, ent->s.origin);

	self->goalentity = self->enemy = ent;
	self->monsterinfo.currentmove = &fixbot_move_takeoff;
}

static void change_to_roam (edict_t *self)
{
	if (fixbot_search (self))
		return;

	self->monsterinfo.currentmove = &fixbot_move_roamgoal;

	if (self->spawnflags & SPAWNFLAG_FIXBOT_LANDING)
	{
		landing_goal (self);
		self->monsterinfo.currentmove = &fixbot_move_landing;
		self->spawnflags = SPAWNFLAG_FIXBOT_WORKING;
	}
	if (self->spawnflags & SPAWNFLAG_FIXBOT_TAKEOFF)
	{
		takeoff_goal (self);
		self->monsterinfo.currentmove = &fixbot_move_takeoff;
		self->spawnflags = SPAWNFLAG_FIXBOT_WORKING;
	}
	if (self->spawnflags & SPAWNFLAG_FIXBOT_FIXIT)
	{
		self->monsterinfo.currentmove = &fixbot_move_roamgoal;
		self->spawnflags = SPAWNFLAG_FIXBOT_WORKING;
	}
	if (!self->spawnflags)
		self->monsterinfo.currentmove = &fixbot_move_stand2;
}

// off to the farthest it can see of twelve ways round
static void roam_goal (edict_t *self)
{
	trace_t	tr;
	vec3_t	forward, right, up;
	vec3_t	end;
	edict_t	*ent;
	vec3_t	dang;
	float	len, oldlen;
	int		i;
	vec3_t	vec;
	vec3_t	whichvec = { 0, 0, 0 };

	ent = fixbot_goal (self);
	ent->nextthink = level.time + FRAMETIME;

	oldlen = 0;

	for (i = 0; i < 12; i++)
	{
		VectorCopy (self->s.angles, dang);

		if (i < 6)
			dang[YAW] += 30 * i;
		else
			dang[YAW] -= 30 * (i - 6);

		AngleVectors (dang, forward, right, up);
		VectorMA (self->s.origin, 8192, forward, end);

		tr = gi.trace (self->s.origin, NULL, NULL, end, self, MASK_SHOT);

		VectorSubtract (self->s.origin, tr.endpos, vec);
		len = VectorNormalize (vec);

		if (len > oldlen)
		{
			oldlen = len;
			VectorCopy (tr.endpos, whichvec);
		}
	}

	VectorCopy (whichvec, ent->s.origin);
	self->goalentity = self->enemy = ent;

	self->monsterinfo.currentmove = &fixbot_move_turn;
}

// done with its goal (a bot_goal): let it go
static void fixbot_drop_goal (edict_t *self)
{
	self->goalentity->nextthink = level.time + FRAMETIME;
	self->goalentity->think = G_FreeEdict;
	self->goalentity = self->enemy = NULL;
}

static void use_scanner (edict_t *self)
{
	edict_t	*ent = NULL;
	float	radius = 1024;
	vec3_t	vec;
	float	len;

	while ((ent = findradius (ent, self->s.origin, radius)) != NULL)
	{
		if (ent->health >= 100)
		{
			if (ent->classname && strcmp (ent->classname, "object_repair") == 0)
			{
				if (visible (self, ent))
				{
					// remove the old one
					if (self->goalentity && strcmp (self->goalentity->classname, "bot_goal") == 0)
					{
						self->goalentity->nextthink = level.time + FRAMETIME;
						self->goalentity->think = G_FreeEdict;
					}

					self->goalentity = self->enemy = ent;

					VectorSubtract (self->s.origin, self->goalentity->s.origin, vec);
					len = VectorNormalize (vec);

					fixbot_set_fly_parameters (self, false, true);

					if (len < 32)
					{
						self->monsterinfo.currentmove = &fixbot_move_weld_start;
						return;
					}
					return;
				}
			}
		}
	}

	if (!self->goalentity)
	{
		self->monsterinfo.currentmove = &fixbot_move_stand;
		return;
	}

	VectorSubtract (self->s.origin, self->goalentity->s.origin, vec);
	len = VectorLength (vec);

	if (len < 32)
	{
		if (strcmp (self->goalentity->classname, "object_repair") == 0)
		{
			self->monsterinfo.currentmove = &fixbot_move_weld_start;
		}
		else
		{
			fixbot_drop_goal (self);
			self->monsterinfo.currentmove = &fixbot_move_stand;
		}
		return;
	}

	VectorSubtract (self->s.origin, self->s.old_origin, vec);
	len = VectorLength (vec);

	// bot is stuck get new goalentity
	if (len == 0)
	{
		if (strcmp (self->goalentity->classname, "object_repair") == 0)
		{
			self->monsterinfo.currentmove = &fixbot_move_stand;
		}
		else
		{
			fixbot_drop_goal (self);
			self->monsterinfo.currentmove = &fixbot_move_stand;
		}
	}
}

/*
	when the bot has found a landing pad
	it will proceed to its goalentity
	just above the landing pad and
	decend translated along the z the current
	frames are at 10fps
*/
static void blastoff (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int te_impact, int hspread, int vspread)
{
	trace_t		tr;
	vec3_t		dir;
	vec3_t		forward, right, up;
	vec3_t		end;
	float		r;
	float		u;
	vec3_t		water_start;
	qboolean	water = false;
	int			content_mask = MASK_SHOT | MASK_WATER;

	hspread += (self->s.frame - FRAME_takeoff_01);
	vspread += (self->s.frame - FRAME_takeoff_01);

	tr = gi.trace (self->s.origin, NULL, NULL, start, self, MASK_SHOT);
	if (!(tr.fraction < 1.0))
	{
		vectoangles (aimdir, dir);
		AngleVectors (dir, forward, right, up);

		r = crandom () * hspread;
		u = crandom () * vspread;
		VectorMA (start, 8192, forward, end);
		VectorMA (end, r, right, end);
		VectorMA (end, u, up, end);

		if (gi.pointcontents (start) & MASK_WATER)
		{
			water = true;
			VectorCopy (start, water_start);
			content_mask &= ~MASK_WATER;
		}

		tr = gi.trace (start, NULL, NULL, end, self, content_mask);

		// see if we hit water
		if (tr.contents & MASK_WATER)
		{
			int color;

			water = true;
			VectorCopy (tr.endpos, water_start);

			if (!VectorCompare (start, tr.endpos))
			{
				if (tr.contents & CONTENTS_WATER)
				{
					if (strcmp (tr.surface->name, "*brwater") == 0)
						color = SPLASH_BROWN_WATER;
					else
						color = SPLASH_BLUE_WATER;
				}
				else if (tr.contents & CONTENTS_SLIME)
					color = SPLASH_SLIME;
				else if (tr.contents & CONTENTS_LAVA)
					color = SPLASH_LAVA;
				else
					color = SPLASH_UNKNOWN;

				if (color != SPLASH_UNKNOWN)
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (TE_SPLASH);
					gi.WriteByte (8);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.WriteByte (color);
					gi.multicast (tr.endpos, MULTICAST_PVS);
				}

				// change bullet's course when it enters water
				VectorSubtract (end, start, dir);
				vectoangles (dir, dir);
				AngleVectors (dir, forward, right, up);
				r = crandom () * hspread * 2;
				u = crandom () * vspread * 2;
				VectorMA (water_start, 8192, forward, end);
				VectorMA (end, r, right, end);
				VectorMA (end, u, up, end);
			}

			// re-trace ignoring water this time
			tr = gi.trace (water_start, NULL, NULL, end, self, MASK_SHOT);
		}
	}

	// send gun puff / flash
	if (!((tr.surface) && (tr.surface->flags & SURF_SKY)))
	{
		if (tr.fraction < 1.0)
		{
			if (tr.ent->takedamage)
			{
				T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_BULLET, MOD_BLASTOFF);
			}
			else
			{
				if (!(tr.surface && (tr.surface->flags & SURF_SKY)))
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (te_impact);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.multicast (tr.endpos, MULTICAST_PVS);
				}
			}
		}
	}

	// if went through water, determine where the end and make a bubble trail
	if (water)
	{
		vec3_t pos;

		VectorSubtract (tr.endpos, water_start, dir);
		VectorNormalize (dir);
		VectorMA (tr.endpos, -2, dir, pos);
		if (gi.pointcontents (pos) & MASK_WATER)
			VectorCopy (pos, tr.endpos);
		else
			tr = gi.trace (pos, NULL, NULL, water_start, tr.ent, MASK_WATER);

		VectorAdd (water_start, tr.endpos, pos);
		VectorScale (pos, 0.5, pos);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BUBBLETRAIL);
		gi.WritePosition (water_start);
		gi.WritePosition (tr.endpos);
		gi.multicast (pos, MULTICAST_PVS);
	}
}

static void fly_vertical (edict_t *self)
{
	int		i;
	vec3_t	v;
	vec3_t	forward, right, up;
	vec3_t	start;
	vec3_t	tempvec;

	if (!self->goalentity)
	{
		fixbot_stand (self);
		return;
	}

	VectorSubtract (self->goalentity->s.origin, self->s.origin, v);
	self->ideal_yaw = vectoyaw (v);
	M_ChangeYaw (self);

	if (self->s.frame == FRAME_landing_58 || self->s.frame == FRAME_takeoff_16)
	{
		fixbot_drop_goal (self);
		self->monsterinfo.currentmove = &fixbot_move_stand;
	}

	// kick up some particles
	VectorCopy (self->s.angles, tempvec);
	tempvec[PITCH] += 90;

	AngleVectors (tempvec, forward, right, up);
	VectorCopy (self->s.origin, start);

	for (i = 0; i < 10; i++)
		blastoff (self, start, forward, 2, 1, TE_SHOTGUN, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD);
}

static void fly_vertical2 (edict_t *self)
{
	vec3_t	v;
	float	len;

	if (!self->goalentity)
	{
		fixbot_stand (self);
		return;
	}

	VectorSubtract (self->goalentity->s.origin, self->s.origin, v);
	len = VectorLength (v);
	self->ideal_yaw = vectoyaw (v);
	M_ChangeYaw (self);

	if (len < 32)
	{
		fixbot_drop_goal (self);
		self->monsterinfo.currentmove = &fixbot_move_stand;
	}
}

static void ai_facing (edict_t *self, float dist)
{
	vec3_t v;

	if (!self->goalentity)
	{
		fixbot_stand (self);
		return;
	}

	if (infront (self, self->goalentity))
		self->monsterinfo.currentmove = &fixbot_move_forward;
	else
	{
		VectorSubtract (self->goalentity->s.origin, self->s.origin, v);
		self->ideal_yaw = vectoyaw (v);
		M_ChangeYaw (self);
	}
}

static void ai_movetogoal (edict_t *self, float dist)
{
	M_MoveToGoal (self, dist);
}

// the healing ray, from in front of it to its patient
static void fixbot_laser_update (edict_t *laser)
{
	edict_t	*self = laser->owner;
	vec3_t	start, dir, point;

	AngleVectors (self->s.angles, dir, NULL, NULL);
	VectorMA (self->s.origin, 16, dir, start);

	if (self->enemy && self->health > 0)
	{
		VectorAdd (self->enemy->absmin, self->enemy->absmax, point);
		VectorScale (point, 0.5, point);
		if (self->monsterinfo.aiflags & AI_MEDIC)
			point[0] += sin (level.time) * 8;
		VectorSubtract (point, self->s.origin, dir);
		VectorNormalize (dir);
	}

	VectorCopy (start, laser->s.origin);
	VectorCopy (dir, laser->movedir);
	gi.linkentity (laser);
	dabeam_update_rr (laser, true);
}

// raise its patient, healed enough: as it was spawned, with the health it had
static void fixbot_raise (edict_t *self)
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

	if (patient->inuse)
	{
		cleanupHealTarget (patient);

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
	}

	self->monsterinfo.currentmove = &fixbot_move_stand;
}

static void fixbot_fire_laser (edict_t *self)
{
	vec3_t	maxs;
	trace_t	tr;

	// critter dun got blown up while bein' fixed
	if (!self->enemy || !self->enemy->inuse || self->enemy->health <= self->enemy->gib_health)
	{
		self->monsterinfo.currentmove = &fixbot_move_stand;
		self->monsterinfo.aiflags &= ~AI_MEDIC;
		return;
	}

	monster_fire_dabeam_rr (self, -1, false, fixbot_laser_update);

	if (self->enemy->health > (self->enemy->mass / 10))
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

		tr = gi.trace (self->enemy->s.origin, self->enemy->mins, maxs, self->enemy->s.origin, self->enemy, MASK_MONSTERSOLID);
		if (tr.startsolid || tr.allsolid)
		{
			abortHeal (self, false, true, false);
			return;
		}
		else if (tr.ent != world)
		{
			abortHeal (self, false, true, false);
			return;
		}

		self->enemy->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT;
		fixbot_raise (self);
	}
	else
		self->enemy->monsterinfo.aiflags |= AI_RESURRECTING;
}

static void weldstate (edict_t *self)
{
	if (self->s.frame == FRAME_weldstart_10)
		self->monsterinfo.currentmove = &fixbot_move_weld;
	else if (self->goalentity && self->s.frame == FRAME_weldmiddle_07)
	{
		if (self->goalentity->health <= 0)
		{
			self->enemy->owner = NULL;
			self->monsterinfo.currentmove = &fixbot_move_weld_end;
		}
		else
			self->goalentity->health -= 10;
	}
	else
	{
		self->goalentity = self->enemy = NULL;
		self->monsterinfo.currentmove = &fixbot_move_stand;
	}
}

static void ai_move2 (edict_t *self, float dist)
{
	vec3_t v;

	if (!self->goalentity)
	{
		fixbot_stand (self);
		return;
	}

	M_walkmove (self, self->s.angles[YAW], dist);

	VectorSubtract (self->goalentity->s.origin, self->s.origin, v);
	self->ideal_yaw = vectoyaw (v);
	M_ChangeYaw (self);
}

static void fixbot_fire_welder (edict_t *self)
{
	vec3_t	start;
	vec3_t	forward, right, up;
	vec3_t	vec;
	float	r;

	if (!self->enemy)
		return;

	vec[0] = 24.0;
	vec[1] = -0.8;
	vec[2] = -10.0;
	AngleVectors (self->s.angles, forward, right, up);
	G_ProjectSource (self->s.origin, vec, forward, right, start);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_WELDING_SPARKS);
	gi.WriteByte (10);
	gi.WritePosition (start);
	gi.WriteDir (vec3_origin);
	gi.WriteByte (0xe0 + (rand () & 7));
	gi.multicast (self->s.origin, MULTICAST_PVS);

	if (random () > 0.8)
	{
		r = random ();

		if (r < 0.33)
			gi.sound (self, CHAN_VOICE, sound_weld1, 1, ATTN_IDLE, 0);
		else if (r < 0.66)
			gi.sound (self, CHAN_VOICE, sound_weld2, 1, ATTN_IDLE, 0);
		else
			gi.sound (self, CHAN_VOICE, sound_weld3, 1, ATTN_IDLE, 0);
	}
}

static void fixbot_fire_blaster (edict_t *self)
{
	vec3_t	start;
	vec3_t	forward, right, up;
	vec3_t	end;
	vec3_t	dir;

	if (!self->enemy)
		return;

	if (!visible (self, self->enemy))
		self->monsterinfo.currentmove = &fixbot_move_run;

	AngleVectors (self->s.angles, forward, right, up);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_HOVER_BLASTER_1], forward, right, start);

	VectorCopy (self->enemy->s.origin, end);
	end[2] += self->enemy->viewheight;
	VectorSubtract (end, start, dir);
	VectorNormalize (dir);

	monster_fire_blaster (self, start, dir, 15, 1000, MZ2_HOVER_BLASTER_1, EF_BLASTER);
}

static void fixbot_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &fixbot_move_stand;
}

static void fixbot_run (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &fixbot_move_stand;
	else
		self->monsterinfo.currentmove = &fixbot_move_run;
}

static void fixbot_walk (edict_t *self)
{
	vec3_t	vec;
	float	len;

	if (self->goalentity && strcmp (self->goalentity->classname, "object_repair") == 0)
	{
		VectorSubtract (self->s.origin, self->goalentity->s.origin, vec);
		len = VectorLength (vec);
		if (len < 32)
		{
			self->monsterinfo.currentmove = &fixbot_move_weld_start;
			return;
		}
	}
	self->monsterinfo.currentmove = &fixbot_move_walk;
}

static void fixbot_attack (edict_t *self)
{
	vec3_t	vec;
	float	len;

	if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		if (!visible (self, self->enemy))
			return;
		VectorSubtract (self->s.origin, self->enemy->s.origin, vec);
		len = VectorLength (vec);
		if (len > 128)
			return;
		else
			self->monsterinfo.currentmove = &fixbot_move_laserattack;
	}
	else
	{
		fixbot_set_fly_parameters (self, false, false);
		self->monsterinfo.currentmove = &fixbot_move_attack2;
	}
}

static void fixbot_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (level.time < self->pain_debounce_time)
		return;

	fixbot_set_fly_parameters (self, false, false);
	self->pain_debounce_time = level.time + 3;
	gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);

	if (damage <= 10)
		self->monsterinfo.currentmove = &fixbot_move_pain3;
	else if (damage <= 25)
		self->monsterinfo.currentmove = &fixbot_move_painb;
	else
		self->monsterinfo.currentmove = &fixbot_move_paina;

	abortHeal (self, false, false, false);
}

static void fixbot_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	gi.sound (self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	BecomeExplosion1 (self);
}

/*QUAKED monster_fixbot (1 .5 0) (-32 -32 -24) (32 32 24) Ambush Trigger_Spawn Fixit Takeoff Landing
 */
void SP_monster_fixbot (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}

	sound_pain1 = gi.soundindex ("flyer/flypain1.wav");
	sound_die = gi.soundindex ("flyer/flydeth1.wav");

	sound_weld1 = gi.soundindex ("misc/welder1.wav");
	sound_weld2 = gi.soundindex ("misc/welder2.wav");
	sound_weld3 = gi.soundindex ("misc/welder3.wav");

	self->s.modelindex = gi.modelindex ("models/monsters/fixbot/tris.md2");

	VectorSet (self->mins, -32, -32, -24);
	VectorSet (self->maxs, 32, 32, 24);

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;

	self->health = 150;
	self->mass = 150;

	self->pain = fixbot_pain;
	self->die = fixbot_die;

	self->monsterinfo.stand = fixbot_stand;
	self->monsterinfo.walk = fixbot_walk;
	self->monsterinfo.run = fixbot_run;
	self->monsterinfo.attack = fixbot_attack;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &fixbot_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;
	self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
	fixbot_set_fly_parameters (self, false, false);

	flymonster_start (self);
}

/*QUAKED func_object_repair (1 .5 0) (-8 -8 -8) (8 8 8)
object to be repaired.
The default delay is 1 second
"delay" the delay in seconds for spark to occur
*/

static void object_repair_fx (edict_t *ent)
{
	ent->nextthink = level.time + ent->delay;

	if (ent->health <= 100)
		ent->health++;
	else
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_WELDING_SPARKS);
		gi.WriteByte (10);
		gi.WritePosition (ent->s.origin);
		gi.WriteDir (vec3_origin);
		gi.WriteByte (0xe0 + (rand () & 7));
		gi.multicast (ent->s.origin, MULTICAST_PVS);
	}
}

static void object_repair_dead (edict_t *ent)
{
	G_UseTargets (ent, ent);
	ent->nextthink = level.time + FRAMETIME;
	ent->think = object_repair_fx;
}

static void object_repair_sparks (edict_t *ent)
{
	if (ent->health <= 0)
	{
		ent->nextthink = level.time + FRAMETIME;
		ent->think = object_repair_dead;
		return;
	}

	ent->nextthink = level.time + ent->delay;

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_WELDING_SPARKS);
	gi.WriteByte (10);
	gi.WritePosition (ent->s.origin);
	gi.WriteDir (vec3_origin);
	gi.WriteByte (0xe0 + (rand () & 7));
	gi.multicast (ent->s.origin, MULTICAST_PVS);
}

void SP_object_repair (edict_t *ent)
{
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_BBOX;
	ent->classname = "object_repair";
	VectorSet (ent->mins, -8, -8, 8);
	VectorSet (ent->maxs, 8, 8, 8);
	ent->think = object_repair_sparks;
	ent->nextthink = level.time + 1;
	ent->health = 100;
	if (!ent->delay)
		ent->delay = 1.0;
}
