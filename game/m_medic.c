/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/*
==============================================================================

MEDIC

==============================================================================
*/

#include "g_local.h"
#include "m_medic.h"

qboolean visible (edict_t *self, edict_t *other);


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

// Ground Zero's medic commander (monster_medic_commander): its own sounds,
// blaster2, and reinforcements it summons
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

#define MEDIC_COMMANDER(self)	((self)->mass > 400)
#define MEDIC_SOUND(self, snd)	(MEDIC_COMMANDER(self) ? commander_##snd : snd)


edict_t *medic_FindDeadMonster (edict_t *self)
{
	edict_t	*ent = NULL;
	edict_t	*best = NULL;

	while ((ent = findradius(ent, self->s.origin, 1024)) != NULL)
	{
		if (ent == self)
			continue;
		if (!(ent->svflags & SVF_MONSTER))
			continue;
		if (ent->monsterinfo.aiflags & AI_GOOD_GUY)
			continue;
		if (ent->owner)
			continue;
		if (ent->health > 0)
			continue;
		if (ent->nextthink)
			continue;
		if (!visible(self, ent))
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

void medic_idle (edict_t *self)
{
	edict_t	*ent;

	gi.sound (self, CHAN_VOICE, MEDIC_SOUND(self, sound_idle1), 1, ATTN_IDLE, 0);

	ent = medic_FindDeadMonster(self);
	if (ent)
	{
		self->enemy = ent;
		self->enemy->owner = self;
		self->monsterinfo.aiflags |= AI_MEDIC;
		FoundTarget (self);
	}
}

void medic_search (edict_t *self)
{
	edict_t	*ent;

	gi.sound (self, CHAN_VOICE, MEDIC_SOUND(self, sound_search), 1, ATTN_IDLE, 0);

	if (!self->oldenemy)
	{
		ent = medic_FindDeadMonster(self);
		if (ent)
		{
			self->oldenemy = self->enemy;
			self->enemy = ent;
			self->enemy->owner = self;
			self->monsterinfo.aiflags |= AI_MEDIC;
			FoundTarget (self);
		}
	}
}

void medic_sight (edict_t *self, edict_t *other)
{
	gi.sound (self, CHAN_VOICE, MEDIC_SOUND(self, sound_sight), 1, ATTN_NORM, 0);
}


mframe_t medic_frames_stand [] =
{
	ai_stand, 0, medic_idle,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,
	ai_stand, 0, NULL,

};
mmove_t medic_move_stand = {FRAME_wait1, FRAME_wait90, medic_frames_stand, NULL};

void medic_stand (edict_t *self)
{
	self->monsterinfo.currentmove = &medic_move_stand;
}


mframe_t medic_frames_walk [] =
{
	ai_walk, 6.2,	NULL,
	ai_walk, 18.1,  NULL,
	ai_walk, 1,		NULL,
	ai_walk, 9,		NULL,
	ai_walk, 10,	NULL,
	ai_walk, 9,		NULL,
	ai_walk, 11,	NULL,
	ai_walk, 11.6,  NULL,
	ai_walk, 2,		NULL,
	ai_walk, 9.9,	NULL,
	ai_walk, 14,	NULL,
	ai_walk, 9.3,	NULL
};
mmove_t medic_move_walk = {FRAME_walk1, FRAME_walk12, medic_frames_walk, NULL};

void medic_walk (edict_t *self)
{
	self->monsterinfo.currentmove = &medic_move_walk;
}


mframe_t medic_frames_run [] =
{
	ai_run, 18,		NULL,
	ai_run, 22.5,	NULL,
	ai_run, 25.4,	NULL,
	ai_run, 23.4,	NULL,
	ai_run, 24,		NULL,
	ai_run, 35.6,	NULL
	
};
mmove_t medic_move_run = {FRAME_run1, FRAME_run6, medic_frames_run, NULL};

void medic_run (edict_t *self)
{
	if (!(self->monsterinfo.aiflags & AI_MEDIC))
	{
		edict_t	*ent;

		ent = medic_FindDeadMonster(self);
		if (ent)
		{
			self->oldenemy = self->enemy;
			self->enemy = ent;
			self->enemy->owner = self;
			self->monsterinfo.aiflags |= AI_MEDIC;
			FoundTarget (self);
			return;
		}
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		self->monsterinfo.currentmove = &medic_move_stand;
	else
		self->monsterinfo.currentmove = &medic_move_run;
}


mframe_t medic_frames_pain1 [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t medic_move_pain1 = {FRAME_paina1, FRAME_paina8, medic_frames_pain1, medic_run};

mframe_t medic_frames_pain2 [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t medic_move_pain2 = {FRAME_painb1, FRAME_painb15, medic_frames_pain2, medic_run};

void medic_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (self->health < (self->max_health / 2))
		self->s.skinnum = MEDIC_COMMANDER(self) ? 3 : 1;

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	if (skill->value == 3)
		return;		// no pain anims in nightmare

	if (MEDIC_COMMANDER(self))
	{
		// it ignores pain while healing, and small hurts
		if (self->monsterinfo.aiflags & AI_MEDIC)
			return;
		if (damage < 35)
		{
			gi.sound (self, CHAN_VOICE, commander_sound_pain1, 1, ATTN_NORM, 0);
			return;
		}

		self->monsterinfo.aiflags &= ~(AI_MANUAL_STEERING|AI_HOLD_FRAME);
		gi.sound (self, CHAN_VOICE, commander_sound_pain2, 1, ATTN_NORM, 0);

		// no more than a half chance of the big pain
		if (random() < (damage * 0.005 < 0.5 ? damage * 0.005 : 0.5))
			self->monsterinfo.currentmove = &medic_move_pain2;
		else
			self->monsterinfo.currentmove = &medic_move_pain1;
		return;
	}

	if (random() < 0.5)
	{
		self->monsterinfo.currentmove = &medic_move_pain1;
		gi.sound (self, CHAN_VOICE, MEDIC_SOUND(self, sound_pain1), 1, ATTN_NORM, 0);
	}
	else
	{
		self->monsterinfo.currentmove = &medic_move_pain2;
		gi.sound (self, CHAN_VOICE, MEDIC_SOUND(self, sound_pain2), 1, ATTN_NORM, 0);
	}
}

void medic_fire_blaster (edict_t *self)
{
	vec3_t	start;
	vec3_t	forward, right;
	vec3_t	end;
	vec3_t	dir;
	int		effect;

	if ((self->s.frame == FRAME_attack9) || (self->s.frame == FRAME_attack12))
		effect = EF_BLASTER;
	else if ((self->s.frame == FRAME_attack19) || (self->s.frame == FRAME_attack22) || (self->s.frame == FRAME_attack25) || (self->s.frame == FRAME_attack28))
		effect = EF_HYPERBLASTER;
	else
		effect = 0;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, monster_flash_offset[MZ2_MEDIC_BLASTER_1], forward, right, start);

	VectorCopy (self->enemy->s.origin, end);
	end[2] += self->enemy->viewheight;
	VectorSubtract (end, start, dir);

	if (MEDIC_COMMANDER(self))
		monster_fire_blaster2 (self, start, dir, 2, 1000, MZ2_MEDIC_BLASTER_2, effect);
	else
		monster_fire_blaster (self, start, dir, 2, 1000, MZ2_MEDIC_BLASTER_1, effect);
}


void medic_dead (edict_t *self)
{
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0;
	gi.linkentity (self);
}

mframe_t medic_frames_death [] =
{
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL,
	ai_move, 0, NULL
};
mmove_t medic_move_death = {FRAME_death1, FRAME_death30, medic_frames_death, medic_dead};

void medic_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	int		n;

	// if we had a pending patient, free him up for another medic
	if ((self->enemy) && (self->enemy->owner == self))
		self->enemy->owner = NULL;

// check for gib
	if (self->health <= self->gib_health)
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		for (n= 0; n < 2; n++)
			ThrowGib (self, "models/objects/gibs/bone/tris.md2", damage, GIB_ORGANIC);
		for (n= 0; n < 4; n++)
			ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		ThrowHead (self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

// regular death
	gi.sound (self, CHAN_VOICE, MEDIC_SOUND(self, sound_die), 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	self->monsterinfo.currentmove = &medic_move_death;
}


void medic_duck_down (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_DUCKED)
		return;
	self->monsterinfo.aiflags |= AI_DUCKED;
	self->maxs[2] -= 32;
	self->takedamage = DAMAGE_YES;
	self->monsterinfo.pausetime = level.time + 1;
	gi.linkentity (self);
}

void medic_duck_hold (edict_t *self)
{
	if (level.time >= self->monsterinfo.pausetime)
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
	else
		self->monsterinfo.aiflags |= AI_HOLD_FRAME;
}

void medic_duck_up (edict_t *self)
{
	self->monsterinfo.aiflags &= ~AI_DUCKED;
	self->maxs[2] += 32;
	self->takedamage = DAMAGE_AIM;
	gi.linkentity (self);
}

mframe_t medic_frames_duck [] =
{
	ai_move, -1,	NULL,
	ai_move, -1,	NULL,
	ai_move, -1,	medic_duck_down,
	ai_move, -1,	medic_duck_hold,
	ai_move, -1,	NULL,
	ai_move, -1,	NULL,
	ai_move, -1,	medic_duck_up,
	ai_move, -1,	NULL,
	ai_move, -1,	NULL,
	ai_move, -1,	NULL,
	ai_move, -1,	NULL,
	ai_move, -1,	NULL,
	ai_move, -1,	NULL,
	ai_move, -1,	NULL,
	ai_move, -1,	NULL,
	ai_move, -1,	NULL
};
mmove_t medic_move_duck = {FRAME_duck1, FRAME_duck16, medic_frames_duck, medic_run};

void medic_dodge (edict_t *self, edict_t *attacker, float eta)
{
	if (random() > 0.25)
		return;

	if (!self->enemy)
		self->enemy = attacker;

	self->monsterinfo.currentmove = &medic_move_duck;
}

mframe_t medic_frames_attackHyperBlaster [] =
{
	ai_charge, 0,	NULL,
	ai_charge, 0,	NULL,
	ai_charge, 0,	NULL,
	ai_charge, 0,	NULL,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	medic_fire_blaster
};
mmove_t medic_move_attackHyperBlaster = {FRAME_attack15, FRAME_attack30, medic_frames_attackHyperBlaster, medic_run};


void medic_continue (edict_t *self)
{
	if (visible (self, self->enemy) )
		if (random() <= 0.95)
			self->monsterinfo.currentmove = &medic_move_attackHyperBlaster;
}


mframe_t medic_frames_attackBlaster [] =
{
	ai_charge, 0,	NULL,
	ai_charge, 5,	NULL,
	ai_charge, 5,	NULL,
	ai_charge, 3,	NULL,
	ai_charge, 2,	NULL,
	ai_charge, 0,	NULL,
	ai_charge, 0,	NULL,
	ai_charge, 0,	NULL,
	ai_charge, 0,	medic_fire_blaster,
	ai_charge, 0,	NULL,
	ai_charge, 0,	NULL,
	ai_charge, 0,	medic_fire_blaster,	
	ai_charge, 0,	NULL,
	ai_charge, 0,	medic_continue	// Change to medic_continue... Else, go to frame 32
};
mmove_t medic_move_attackBlaster = {FRAME_attack1, FRAME_attack14, medic_frames_attackBlaster, medic_run};


void medic_hook_launch (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, MEDIC_SOUND(self, sound_hook_launch), 1, ATTN_NORM, 0);
}

void ED_CallSpawn (edict_t *ent);

static vec3_t	medic_cable_offsets[] =
{
	45.0,  -9.2, 15.5,
	48.4,  -9.7, 15.2,
	47.8,  -9.8, 15.8,
	47.3,  -9.3, 14.3,
	45.4, -10.1, 13.1,
	41.9, -12.7, 12.0,
	37.8, -15.8, 11.2,
	34.3, -18.4, 10.7,
	32.7, -19.7, 10.4,
	32.7, -19.7, 10.4
};

void medic_cable_attack (edict_t *self)
{
	vec3_t	offset, start, end, f, r;
	trace_t	tr;
	vec3_t	dir, angles;
	float	distance;

	if (!self->enemy->inuse)
		return;

	AngleVectors (self->s.angles, f, r, NULL);
	VectorCopy (medic_cable_offsets[self->s.frame - FRAME_attack42], offset);
	G_ProjectSource (self->s.origin, offset, f, r, start);

	// check for max distance
	VectorSubtract (start, self->enemy->s.origin, dir);
	distance = VectorLength(dir);
	if (distance > 256)
		return;

	// check for min/max pitch
	vectoangles (dir, angles);
	if (angles[0] < -180)
		angles[0] += 360;
	if (fabs(angles[0]) > 45)
		return;

	tr = gi.trace (start, NULL, NULL, self->enemy->s.origin, self, MASK_SHOT);
	if (tr.fraction != 1.0 && tr.ent != self->enemy)
		return;

	if (self->s.frame == FRAME_attack43)
	{
		gi.sound (self->enemy, CHAN_AUTO, MEDIC_SOUND(self, sound_hook_hit), 1, ATTN_NORM, 0);
		self->enemy->monsterinfo.aiflags |= AI_RESURRECTING;
	}
	else if (self->s.frame == FRAME_attack50)
	{
		self->enemy->spawnflags = 0;
		self->enemy->monsterinfo.aiflags = 0;
		self->enemy->target = NULL;
		self->enemy->targetname = NULL;
		self->enemy->combattarget = NULL;
		self->enemy->deathtarget = NULL;
		self->enemy->owner = self;
		ED_CallSpawn (self->enemy);
		self->enemy->owner = NULL;
		if (self->enemy->think)
		{
			self->enemy->nextthink = level.time;
			self->enemy->think (self->enemy);
		}
		self->enemy->monsterinfo.aiflags |= AI_RESURRECTING;
		if (self->oldenemy && self->oldenemy->client)
		{
			self->enemy->enemy = self->oldenemy;
			FoundTarget (self->enemy);
		}
	}
	else
	{
		if (self->s.frame == FRAME_attack44)
			gi.sound (self, CHAN_WEAPON, MEDIC_SOUND(self, sound_hook_heal), 1, ATTN_NORM, 0);
	}

	// adjust start for beam origin being in middle of a segment
	VectorMA (start, 8, f, start);

	// adjust end z for end spot since the monster is currently dead
	VectorCopy (self->enemy->s.origin, end);
	end[2] = self->enemy->absmin[2] + self->enemy->size[2] / 2;

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_MEDIC_CABLE_ATTACK);
	gi.WriteShort (self - g_edicts);
	gi.WritePosition (start);
	gi.WritePosition (end);
	gi.multicast (self->s.origin, MULTICAST_PVS);
}

void medic_hook_retract (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, MEDIC_SOUND(self, sound_hook_retract), 1, ATTN_NORM, 0);
	self->enemy->monsterinfo.aiflags &= ~AI_RESURRECTING;
}

mframe_t medic_frames_attackCable [] =
{
	ai_move, 2,		NULL,
	ai_move, 3,		NULL,
	ai_move, 5,		NULL,
	ai_move, 4.4,	NULL,
	ai_charge, 4.7,	NULL,
	ai_charge, 5,	NULL,
	ai_charge, 6,	NULL,
	ai_charge, 4,	NULL,
	ai_charge, 0,	NULL,
	ai_move, 0,		medic_hook_launch,
	ai_move, 0,		medic_cable_attack,
	ai_move, 0,		medic_cable_attack,
	ai_move, 0,		medic_cable_attack,
	ai_move, 0,		medic_cable_attack,
	ai_move, 0,		medic_cable_attack,
	ai_move, 0,		medic_cable_attack,
	ai_move, 0,		medic_cable_attack,
	ai_move, 0,		medic_cable_attack,
	ai_move, 0,		medic_cable_attack,
	ai_move, -15,	medic_hook_retract,
	ai_move, -1.5,	NULL,
	ai_move, -1.2,	NULL,
	ai_move, -3,	NULL,
	ai_move, -2,	NULL,
	ai_move, 0.3,	NULL,
	ai_move, 0.7,	NULL,
	ai_move, 1.2,	NULL,
	ai_move, 1.3,	NULL
};
mmove_t medic_move_attackCable = {FRAME_attack33, FRAME_attack60, medic_frames_attackCable, medic_run};


/*
=============================================================================

The medic commander's reinforcements, as the re-release has them: its
list (by default soldiers to a gladiator, or the map's), as many as it
has room for, in the spots before it -- or behind it, turning round first.

=============================================================================
*/
static char *default_reinforcements = "monster_soldier_light 1;monster_soldier 2;monster_soldier_ss 2;monster_infantry 3;monster_gunner 4;monster_medic 5;monster_gladiator 6";
#define	DEFAULT_MONSTER_SLOTS_BASE	3

static vec3_t reinforcement_position[MAX_REINFORCEMENTS] = {
	{80, 0, 0}, {40, 60, 0}, {40, -60, 0}, {0, 80, 0}, {0, -80, 0}
};

// how many it readied
static int medic_num_chosen (edict_t *self)
{
	int		n;

	for (n = 0; n < MAX_REINFORCEMENTS; n++)
		if (self->monsterinfo.chosen_reinforcements[n] == 255)
			break;
	return n;
}

// is there room for the count'th, at its spot before it (or behind)?
static qboolean medic_spawn_spot (edict_t *self, int count, qboolean behind, qboolean ground, vec3_t spawnpoint)
{
	vec3_t	f, r, offset, startpoint, mins, maxs;
	int		which = self->monsterinfo.chosen_reinforcements[count];

	VectorCopy (self->monsterinfo.reinforcements[which].mins, mins);
	VectorCopy (self->monsterinfo.reinforcements[which].maxs, maxs);

	AngleVectors (self->s.angles, f, r, NULL);
	VectorCopy (reinforcement_position[count], offset);
	if (behind)
	{
		offset[0] *= -1.0;
		offset[1] *= -1.0;
	}
	G_ProjectSource (self->s.origin, offset, f, r, startpoint);
	startpoint[2] += 10;		// a little off the ground

	if (!FindSpawnPoint (startpoint, mins, maxs, spawnpoint, 32))
		return false;
	if (ground)
		return CheckGroundSpawnPoint (spawnpoint, mins, maxs, 256, -1);
	return CheckSpawnPoint (spawnpoint, mins, maxs);
}

void medic_start_spawn (edict_t *self)
{
	gi.sound (self, CHAN_WEAPON, commander_sound_spawn, 1, ATTN_NORM, 0);
	self->monsterinfo.nextframe = FRAME_attack48;
}

void medic_determine_spawn (edict_t *self)
{
	vec3_t	spawnpoint;
	int		count, num_summoned;
	qboolean	found = false;

	num_summoned = M_PickReinforcements (self, 0);

	for (count = 0; count < num_summoned && !found; count++)
		found = medic_spawn_spot (self, count, false, true, spawnpoint);

	if (!found)
	{
		// try behind, and turn round to face there
		for (count = 0; count < num_summoned && !found; count++)
			found = medic_spawn_spot (self, count, true, true, spawnpoint);

		if (found)
		{
			self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
			self->ideal_yaw = anglemod(self->s.angles[YAW]) + 180;
			if (self->ideal_yaw > 360.0)
				self->ideal_yaw -= 360.0;
		}
	}

	if (!found)
		self->monsterinfo.nextframe = FRAME_attack53;
}

void medic_spawngrows (edict_t *self)
{
	vec3_t	spawnpoint;
	int		count, num_summoned, num_success = 0, which;
	float	current_yaw;

	// if it was turning round, wait till it has
	if (self->monsterinfo.aiflags & AI_MANUAL_STEERING)
	{
		current_yaw = anglemod(self->s.angles[YAW]);
		if (fabs(current_yaw - self->ideal_yaw) > 0.1)
		{
			self->monsterinfo.aiflags |= AI_HOLD_FRAME;
			return;
		}
		self->monsterinfo.aiflags &= ~(AI_HOLD_FRAME|AI_MANUAL_STEERING);
	}

	num_summoned = medic_num_chosen (self);

	for (count = 0; count < num_summoned; count++)
	{
		if (!medic_spawn_spot (self, count, false, true, spawnpoint))
			continue;
		num_success++;
		which = self->monsterinfo.chosen_reinforcements[count];
		VectorAdd (spawnpoint, self->monsterinfo.reinforcements[which].mins, spawnpoint);
		VectorAdd (spawnpoint, self->monsterinfo.reinforcements[which].maxs, spawnpoint);
		SpawnGrow_Spawn (spawnpoint, 0);
	}

	if (!num_success)
		self->monsterinfo.nextframe = FRAME_attack53;
}

void medic_finish_spawn (edict_t *self)
{
	edict_t	*ent, *designated_enemy;
	vec3_t	spawnpoint;
	int		count, num_summoned, which;

	num_summoned = medic_num_chosen (self);

	for (count = 0; count < num_summoned; count++)
	{
		which = self->monsterinfo.chosen_reinforcements[count];
		if (!medic_spawn_spot (self, count, false, false, spawnpoint))
			continue;
		ent = CreateGroundMonster (spawnpoint, self->s.angles,
			self->monsterinfo.reinforcements[which].mins, self->monsterinfo.reinforcements[which].maxs,
			self->monsterinfo.reinforcements[which].classname, 256);
		if (!ent)
			continue;

		if (ent->think)
		{
			ent->nextthink = level.time;
			ent->think (ent);
		}

		ent->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT | AI_SPAWNED_MEDIC_C;
		ent->monsterinfo.commander = self;
		ent->monsterinfo.monster_slots = self->monsterinfo.reinforcements[which].strength;
		self->monsterinfo.monster_used += self->monsterinfo.reinforcements[which].strength;

		if (self->monsterinfo.aiflags & AI_MEDIC)
			designated_enemy = self->oldenemy;
		else
			designated_enemy = self->enemy;

		if (coop->value)
		{
			// a player it can see, rather than its commander's, if it can
			designated_enemy = PickCoopTarget (ent);
			if (designated_enemy)
			{
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

		if (designated_enemy && designated_enemy->inuse && designated_enemy->health > 0)
		{
			ent->enemy = designated_enemy;
			FoundTarget (ent);
		}
		else
		{
			ent->enemy = NULL;
			ent->monsterinfo.stand (ent);
		}
	}
}

mframe_t medic_frames_callReinforcements [] =
{
	// FRAME_attack33 to FRAME_attack60; 43 to 47 are skipped
	ai_charge, 2,		NULL,
	ai_charge, 3,		NULL,
	ai_charge, 5,		NULL,
	ai_charge, 4.4,		NULL,
	ai_charge, 4.7,		NULL,
	ai_charge, 5,		NULL,
	ai_charge, 6,		NULL,
	ai_charge, 4,		NULL,
	ai_charge, 0,		NULL,
	ai_move, 0,			medic_start_spawn,		// 42
	ai_move, 0,			NULL,
	ai_move, 0,			NULL,
	ai_move, 0,			NULL,
	ai_move, 0,			NULL,
	ai_move, 0,			NULL,
	ai_move, 0,			medic_determine_spawn,	// 48
	ai_charge, 0,		medic_spawngrows,		// 49
	ai_move, 0,			NULL,
	ai_move, 0,			NULL,
	ai_move, -15,		medic_finish_spawn,		// 52
	ai_move, -1.5,		NULL,
	ai_move, -1.2,		NULL,
	ai_move, -3,		NULL,
	ai_move, -2,		NULL,
	ai_move, 0.3,		NULL,
	ai_move, 0.7,		NULL,
	ai_move, 1.2,		NULL,
	ai_move, 1.3,		NULL
};
mmove_t medic_move_callReinforcements = {FRAME_attack33, FRAME_attack60, medic_frames_callReinforcements, medic_run};


void medic_attack(edict_t *self)
{
	float	r;

	if (!MEDIC_COMMANDER(self))
	{
		if (self->monsterinfo.aiflags & AI_MEDIC)
			self->monsterinfo.currentmove = &medic_move_attackCable;
		else
			self->monsterinfo.currentmove = &medic_move_attackBlaster;
		return;
	}

	// the commander: its checkattack's signal to summon
	if (self->monsterinfo.aiflags & AI_BLOCKED)
	{
		self->monsterinfo.currentmove = &medic_move_callReinforcements;
		self->monsterinfo.aiflags &= ~AI_BLOCKED;
	}

	r = random();
	if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		if (r > 0.8 && M_SlotsLeft (self))
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
		if (r > 0.2 && range_to (self, self->enemy) > 20 && M_SlotsLeft (self))
			self->monsterinfo.currentmove = &medic_move_callReinforcements;
		else
			self->monsterinfo.currentmove = &medic_move_attackBlaster;
	}
}

qboolean medic_checkattack (edict_t *self)
{
	if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		medic_attack(self);
		return true;
	}

	if (MEDIC_COMMANDER(self))
	{
		// the commander summons for an unseen player; and, with much room
		// left, a large bias to summon at a distance
		if (self->enemy && self->enemy->client && !visible (self, self->enemy)
			&& M_SlotsLeft (self))
		{
			self->monsterinfo.attack_state = AS_BLIND;
			return true;
		}
		if (self->monsterinfo.monster_slots && random() < 0.8
			&& M_SlotsLeft (self) > self->monsterinfo.monster_slots * 0.8
			&& realrange (self, self->enemy) > 150)
		{
			self->monsterinfo.aiflags |= AI_BLOCKED;
			self->monsterinfo.attack_state = AS_MISSILE;
			return true;
		}
		// its idle looks bad in a fight: on a combat point, always attack
		if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		{
			self->monsterinfo.attack_state = AS_MISSILE;
			return true;
		}
	}

	return M_CheckAttack (self);
}

// the commander's spawn-grow
static void MedicCommanderCache (void)
{
	gi.modelindex ("models/items/spawngro/tris.md2");
	gi.modelindex ("models/items/spawngro2/tris.md2");
}


/*QUAKED monster_medic (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
*/
/*QUAKED monster_medic_commander (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
Ground Zero's: it heals, and summons reinforcements
*/
void SP_monster_medic (edict_t *self)
{
	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}

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

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex ("models/monsters/medic/tris.md2");
	VectorSet (self->mins, -24, -24, -24);
	VectorSet (self->maxs, 24, 24, 32);

	if (!strcmp(self->classname, "monster_medic_commander"))
	{
		self->health = 600;
		self->gib_health = -130;
		self->mass = 600;
		self->yaw_speed = 40;
		MedicCommanderCache ();
	}
	else
	{
		self->health = 300;
		self->gib_health = -130;
		self->mass = 400;
	}

	self->pain = medic_pain;
	self->die = medic_die;

	self->monsterinfo.stand = medic_stand;
	self->monsterinfo.walk = medic_walk;
	self->monsterinfo.run = medic_run;
	self->monsterinfo.dodge = medic_dodge;
	self->monsterinfo.attack = medic_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = medic_sight;
	self->monsterinfo.idle = medic_idle;
	self->monsterinfo.search = medic_search;
	self->monsterinfo.checkattack = medic_checkattack;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &medic_move_stand;
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start (self);

	if (MEDIC_COMMANDER(self))
	{
		self->s.skinnum = 2;
		// its reinforcements: the map's, or soldiers to a gladiator
		M_SetupReinforcements (self, default_reinforcements, DEFAULT_MONSTER_SLOTS_BASE);
		// Ground Zero's blind fire: it summons for a player it can't see
		self->monsterinfo.blindfire = true;

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
	}
}
