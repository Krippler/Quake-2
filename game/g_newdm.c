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
g_newdm.c -- Ground Zero's deathmatch games ("gamerules"), as the 2023
re-release has them (its rogue_dm_tag.cpp and g_rogue_newdm.cpp, GPL):
the hooks a game sets in DMGame, which the base game calls where they
are; and tag, gamerules 1.

Tag: one player has the token (dm_tag_token, or one put at a spawn point
if the map has none). Picking it up heals them to full and gives them body
armour; while they have it, every frag of theirs is 3, every fifth a quad
damage, and fragging them 5 and the token. Damage that is neither to nor
from them is three quarters. Dead, they drop it; it comes back to a spawn
point if no one has it in 30 seconds (3 in lava or slime). Its holder trails
the tag effect and has the tag3 dogtag on the scoreboard.

Its deathball, gamerules 2, is not here: none of these maps has its ball.
*/

#include "g_local.h"

#define RDM_TAG			1
#define RDM_DEATHBALL	2

cvar_t		*gamerules;
dm_game_rt	DMGame;

static edict_t	*tag_token;
static edict_t	*tag_owner;
static int		tag_count;

static gitem_t *Tag_Item (void)
{
	return FindItem ("Tag Token");
}

// the token, thrown from the player, touchable after a second
static void Tag_Respawn (edict_t *ent)
{
	edict_t	*spot = SelectDeathmatchSpawnPoint ();

	if (!spot)
	{
		ent->nextthink = level.time + 1;
		return;
	}
	VectorCopy (spot->s.origin, ent->s.origin);
	gi.linkentity (ent);
}

static void Tag_MakeTouchable (edict_t *ent)
{
	ent->touch = Touch_Item;
	ent->think = Tag_Respawn;

	// back sooner from lava or slime
	if (gi.pointcontents (ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME))
		ent->nextthink = level.time + 3;
	else
		ent->nextthink = level.time + 30;
}

void Tag_DropToken (edict_t *ent, gitem_t *item)
{
	trace_t	trace;
	vec3_t	forward, right, offset;

	tag_count = 0;
	tag_owner = NULL;

	tag_token = G_Spawn ();
	tag_token->classname = item->classname;
	tag_token->item = item;
	tag_token->spawnflags = DROPPED_ITEM;
	tag_token->s.effects = EF_ROTATE | EF_TAGTRAIL;
	tag_token->s.renderfx = RF_GLOW;
	VectorSet (tag_token->mins, -15, -15, -15);
	VectorSet (tag_token->maxs, 15, 15, 15);
	gi.setmodel (tag_token, item->world_model);
	tag_token->solid = SOLID_TRIGGER;
	tag_token->movetype = MOVETYPE_TOSS;
	tag_token->touch = NULL;
	tag_token->owner = ent;

	AngleVectors (ent->client->v_angle, forward, right, NULL);
	VectorSet (offset, 24, 0, -16);
	G_ProjectSource (ent->s.origin, offset, forward, right, tag_token->s.origin);
	trace = gi.trace (ent->s.origin, tag_token->mins, tag_token->maxs,
		tag_token->s.origin, ent, CONTENTS_SOLID);
	VectorCopy (trace.endpos, tag_token->s.origin);

	VectorScale (forward, 100, tag_token->velocity);
	tag_token->velocity[2] = 300;

	tag_token->think = Tag_MakeTouchable;
	tag_token->nextthink = level.time + 1;

	gi.linkentity (tag_token);

	ent->client->pers.inventory[ITEM_INDEX (item)]--;
}

// to full health, and body armour
static void Tag_KillItBonus (edict_t *self)
{
	edict_t	*armor;

	if (self->health < self->max_health)
	{
		self->health += 200;
		if (self->health > self->max_health)
			self->health = self->max_health;
	}

	armor = G_Spawn ();
	armor->spawnflags |= DROPPED_ITEM;
	armor->item = FindItem ("Body Armor");
	Touch_Item (armor, self, NULL, NULL);
	if (armor->inuse)
		G_FreeEdict (armor);
}

qboolean Tag_PickupToken (edict_t *ent, edict_t *other)
{
	if (gamerules->value != RDM_TAG)
		return false;

	tag_token = ent;
	other->client->pers.inventory[ITEM_INDEX (ent->item)]++;
	tag_owner = other;
	tag_count = 0;

	Tag_KillItBonus (other);
	return true;
}

static void Tag_PlayerDeath (edict_t *targ, edict_t *inflictor, edict_t *attacker)
{
	if (tag_token && targ && targ == tag_owner)
	{
		Tag_DropToken (targ, Tag_Item ());
		tag_owner = NULL;
		tag_count = 0;
	}
}

static void Tag_PlayerDisconnect (edict_t *self)
{
	Tag_PlayerDeath (self, NULL, NULL);
}

static void Tag_Score (edict_t *attacker, edict_t *victim, int scoreChange, int mod)
{
	gitem_t	*quad;

	if (tag_token && tag_owner)
	{
		// the holder fragged someone: 3, and every fifth a quad
		if (scoreChange > 0 && tag_owner == attacker)
		{
			scoreChange = 3;
			if (++tag_count == 5)
			{
				quad = FindItem ("Quad Damage");
				attacker->client->pers.inventory[ITEM_INDEX (quad)]++;
				quad->use (attacker, quad);
				tag_count = 0;
			}
		}
		// the holder fragged: 5, and the token goes to who did it
		else if (tag_owner == victim && tag_owner != attacker)
		{
			scoreChange = 5;
			if (mod == MOD_HUNTER_SPHERE || mod == MOD_DOPPLE_EXPLODE ||
				mod == MOD_DOPPLE_VENGEANCE || mod == MOD_DOPPLE_HUNTER ||
				attacker->health <= 0)
			{
				Tag_DropToken (tag_owner, Tag_Item ());
				tag_owner = NULL;
				tag_count = 0;
			}
			else
			{
				Tag_KillItBonus (attacker);
				tag_owner = attacker;
				tag_count = 0;
			}
		}
	}

	attacker->client->resp.score += scoreChange;
}

static void Tag_PlayerEffects (edict_t *ent)
{
	if (ent == tag_owner)
		ent->s.effects |= EF_TAGTRAIL;
}

static void Tag_DogTag (edict_t *ent, edict_t *killer, char **pic)
{
	if (ent == tag_owner)
		*pic = "tag3";
}

// damage neither to nor from the holder is three quarters
static int Tag_ChangeDamage (edict_t *targ, edict_t *attacker, int damage, int mod)
{
	if (targ != tag_owner && attacker != tag_owner)
		return damage * 3 / 4;
	return damage;
}

static void Tag_GameInit (void)
{
	tag_token = NULL;
	tag_owner = NULL;
	tag_count = 0;
}

// a token at a spawn point if the map has none
static void Tag_PostInitSetup (void)
{
	edict_t	*e;
	vec3_t	origin, angles;

	if (G_Find (NULL, FOFS(classname), "dm_tag_token"))
		return;

	e = G_Spawn ();
	e->classname = "dm_tag_token";
	SelectSpawnPoint (e, origin, angles);
	VectorCopy (origin, e->s.origin);
	VectorCopy (origin, e->s.old_origin);
	VectorCopy (angles, e->s.angles);
	SP_dm_tag_token (e);
}

/*QUAKED dm_tag_token (.3 .3 1) (-16 -16 -16) (16 16 16)
The tag token for deathmatch tag games (gamerules 1).
*/
void SP_dm_tag_token (edict_t *self)
{
	if (!deathmatch->value || gamerules->value != RDM_TAG)
	{
		G_FreeEdict (self);
		return;
	}

	tag_token = self;
	tag_count = 0;

	self->classname = "dm_tag_token";
	self->model = "models/items/tagtoken/tris.md2";
	self->count = 1;
	SpawnItem (self, Tag_Item ());
}

/*
The game's hooks, for its gamerules; each level anew (SpawnEntities)
*/
void InitGameRules (void)
{
	memset (&DMGame, 0, sizeof(DMGame));

	if (!deathmatch->value)
		return;

	switch ((int)gamerules->value)
	{
	case 0:
		break;
	case RDM_TAG:
		DMGame.GameInit = Tag_GameInit;
		DMGame.PostInitSetup = Tag_PostInitSetup;
		DMGame.PlayerDeath = Tag_PlayerDeath;
		DMGame.Score = Tag_Score;
		DMGame.PlayerEffects = Tag_PlayerEffects;
		DMGame.DogTag = Tag_DogTag;
		DMGame.PlayerDisconnect = Tag_PlayerDisconnect;
		DMGame.ChangeDamage = Tag_ChangeDamage;
		break;
	default:		// deathball, or none of them
		gi.dprintf ("gamerules %i is not here\n", (int)gamerules->value);
		gi.cvar_forceset ("gamerules", "0");
		break;
	}

	if (DMGame.GameInit)
		DMGame.GameInit ();
}

// a score changing, through the game's own hook if it has one
void G_Score (edict_t *scorer, edict_t *victim, int change)
{
	if (DMGame.Score)
		DMGame.Score (scorer, victim, change, meansOfDeath);
	else
		scorer->client->resp.score += change;
}
