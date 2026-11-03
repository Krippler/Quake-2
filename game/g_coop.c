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
g_coop.c -- the 2023 re-release's co-op, as its game code has it (its
p_client.cpp, g_items.cpp, g_monster.cpp and g_main.cpp, GPL):

g_coop_instanced_items: each item is each player's: one player taking it
	leaves it there for the rest, and each sees only the ones they have not
	taken (SVF_INSTANCED, EntityVisibleToClient). A player who dies keeps
	what they had and comes back with their full health.
g_coop_squad_respawn: a dead player comes back beside a living one, once
	that one is out of a fight and on firm ground, rather than at the
	start; meanwhile they watch. It makes items instanced too, so nobody
	has to go back for theirs.
g_coop_enable_lives, g_coop_num_lives: so many lives each (the number, and
	one more); out of them, a player watches until the level is left. When
	every player is dead with none left, the level starts again
	(restart_level) as they entered it.
g_coop_player_collision: players block each other and their own shots;
	off by default, when they and their shots pass through each other
	(CONTENTS_NOCLIENTS, CS_NO_PLAYER_COLLISION). g_disable_player_collision
	turns it off in deathmatch too.
g_coop_health_scaling: monsters' health goes up by so much of itself for
	each player there is past the first.
*/

#include "g_local.h"

#define	STEPSIZE	18

cvar_t	*g_coop_player_collision;
cvar_t	*g_coop_squad_respawn;
cvar_t	*g_coop_enable_lives;
cvar_t	*g_coop_num_lives;
cvar_t	*g_coop_instanced_items;
cvar_t	*g_coop_health_scaling;
cvar_t	*g_disable_player_collision;

// a dead player put beside a living one (PutClientInServer)
qboolean	use_squad_respawn;
vec3_t		squad_respawn_position, squad_respawn_angles;

void CoopInit (void)
{
	g_coop_player_collision = gi.cvar ("g_coop_player_collision", "0", CVAR_LATCH);
	g_coop_squad_respawn = gi.cvar ("g_coop_squad_respawn", "1", CVAR_LATCH);
	g_coop_enable_lives = gi.cvar ("g_coop_enable_lives", "0", CVAR_LATCH);
	g_coop_num_lives = gi.cvar ("g_coop_num_lives", "2", CVAR_LATCH);
	g_coop_instanced_items = gi.cvar ("g_coop_instanced_items", "1", CVAR_LATCH);
	g_coop_health_scaling = gi.cvar ("g_coop_health_scaling", "0", CVAR_LATCH);
	g_disable_player_collision = gi.cvar ("g_disable_player_collision", "0", 0);
}

static qboolean CoopLives (void)
{
	return coop->value && g_coop_enable_lives->value;
}

/*
=================
G_UseCoopInstancedItems

Squad respawn wants them too, so that nobody need go back for their things
=================
*/
qboolean G_UseCoopInstancedItems (void)
{
	return coop->value && (g_coop_instanced_items->value || g_coop_squad_respawn->value);
}

/*
=================
G_ShouldPlayersCollide

Whether players block each other (weaponry: and each other's shots)
=================
*/
qboolean G_ShouldPlayersCollide (qboolean weaponry)
{
	if (g_disable_player_collision->value)
		return false;
	if (!coop->value)
		return true;
	// shots hit if friendly fire is on (not dmflags' DF_NO_FRIENDLY_FIRE)
	if (weaponry && !((int)dmflags->value & DF_NO_FRIENDLY_FIRE))
		return true;
	return g_coop_player_collision->value != 0;
}

// a player's trace or shot's mask, through the other players when they do not
// collide
int G_ClientMask (edict_t *self, int mask, qboolean weaponry)
{
	if (self && self->client && !G_ShouldPlayersCollide (weaponry))
		mask |= CONTENTS_NOCLIENTS;
	return mask;
}

/*
=================
EntityVisibleToClient

The server's question for an SVF_INSTANCED entity: an item this player has
not taken
=================
*/
qboolean EntityVisibleToClient (edict_t *ent, edict_t *client)
{
	int		n = client - g_edicts - 1;

	if (n < 0 || n >= MAX_CLIENTS)
		return true;
	return !(ent->item_picked_up_by[n >> 5] & (1u << (n & 31)));
}

qboolean G_ItemPickedUpBy (edict_t *ent, edict_t *player)
{
	return !EntityVisibleToClient (ent, player);
}

void G_SetItemPickedUpBy (edict_t *ent, edict_t *player)
{
	int		n = player - g_edicts - 1;

	if (n >= 0 && n < MAX_CLIENTS)
		ent->item_picked_up_by[n >> 5] |= 1u << (n & 31);
}

/*
=================
G_MonsterScaleCoopHealth

So much more health for each player past the first (g_coop_health_scaling),
as players join
=================
*/
void G_MonsterScaleCoopHealth (edict_t *self)
{
	int		delta, add;

	if (self->monsterinfo.health_scaling >= level.coop_scale_players)
		return;
	if (!self->monsterinfo.base_health)
		self->monsterinfo.base_health = self->max_health;

	delta = level.coop_scale_players - self->monsterinfo.health_scaling;
	add = delta * (int)(self->monsterinfo.base_health * level.coop_health_scaling);

	self->health = self->health + add < 1 ? 1 : self->health + add;
	self->max_health += add;
	self->monsterinfo.health_scaling = level.coop_scale_players;
}

void G_CheckCoopHealthScaling (void)
{
	int		i;
	edict_t	*e;

	for (i = game.maxclients + 1, e = g_edicts + i ; i < globals.num_edicts ; i++, e++)
		if (e->inuse && (e->svflags & SVF_MONSTER) && e->health > 0
			&& e->monsterinfo.health_scaling)
			G_MonsterScaleCoopHealth (e);
}

//==========================================================================

// whether any monster is after this player (NULL: any player)
static qboolean G_MonstersSearchingFor (edict_t *player)
{
	int		i;
	edict_t	*e;

	for (i = game.maxclients + 1, e = g_edicts + i ; i < globals.num_edicts ; i++, e++)
	{
		if (!e->inuse || !(e->svflags & SVF_MONSTER) || e->health <= 0)
			continue;
		if (!player && e->enemy && !e->enemy->client)
			continue;
		else if (player && e->enemy != player)
			continue;
		// lost sight of them a while ago
		if ((e->monsterinfo.aiflags & AI_LOST_SIGHT) && level.time > e->monsterinfo.trail_time + 5)
			continue;
		return true;
	}
	return false;
}

/*
=================
G_FindRespawnSpot

A place behind a player to put another: five boxes thrown back from them,
the first to land on the world's firm, dry ground they can see
=================
*/
static qboolean G_FindRespawnSpot (edict_t *player, vec3_t spot)
{
	static const float	yaw_spread[] = { 0, 90, 45, -45, -90 };
	vec3_t	mins = {-16, -16, -24}, maxs = {16, 16, 32};
	vec3_t	angles, start, end, fwd, eye, eye2;
	trace_t	tr;
	int		i, mask = MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME;
	float	zdiff;

	tr = gi.trace (player->s.origin, mins, maxs, player->s.origin, player, MASK_PLAYERSOLID);
	if (tr.startsolid || tr.allsolid)
		return false;

	for (i = 0 ; i < 5 ; i++)
	{
		VectorSet (angles, 0, player->s.angles[YAW] + 180 + yaw_spread[i], 0);

		// up, then back, then down
		VectorCopy (player->s.origin, start);
		VectorCopy (start, end);
		end[2] += 128;
		tr = gi.trace (start, mins, maxs, end, player, mask);
		if (tr.startsolid || tr.allsolid || (tr.contents & (CONTENTS_LAVA | CONTENTS_SLIME)))
			continue;

		AngleVectors (angles, fwd, NULL, NULL);
		VectorCopy (tr.endpos, start);
		VectorMA (start, 128, fwd, end);
		tr = gi.trace (start, mins, maxs, end, player, mask);
		if (tr.startsolid || tr.allsolid || (tr.contents & (CONTENTS_LAVA | CONTENTS_SLIME)))
			continue;

		VectorCopy (tr.endpos, start);
		VectorCopy (start, end);
		end[2] -= 128 * 4;
		tr = gi.trace (start, mins, maxs, end, player, mask);
		if (tr.startsolid || tr.allsolid || (tr.contents & (CONTENTS_LAVA | CONTENTS_SLIME))
			|| tr.fraction == 1.0 || tr.ent != g_edicts)
			continue;

		VectorCopy (tr.endpos, eye);
		eye[2] += 22;
		if (gi.pointcontents (eye) & MASK_WATER)
			continue;
		if (tr.plane.normal[2] < 0.7)
			continue;

		VectorCopy (tr.endpos, spot);

		zdiff = fabs (player->s.origin[2] - tr.endpos[2]);
		if (zdiff > STEPSIZE * 4)
			continue;
		// a step up or down: they must still see each other
		if (zdiff > STEPSIZE)
		{
			tr = gi.trace (player->s.origin, vec3_origin, vec3_origin, spot, player, mask);
			if (tr.fraction != 1.0)
				continue;
			VectorCopy (player->s.origin, eye);
			eye[2] += 22;
			VectorCopy (spot, eye2);
			eye2[2] += 22;
			tr = gi.trace (eye, vec3_origin, vec3_origin, eye2, player, mask);
			if (tr.fraction != 1.0)
				continue;
		}
		return true;
	}
	return false;
}

// a living player to put another beside, and where
static edict_t *G_FindSquadRespawnTarget (vec3_t spot)
{
	qboolean	searching = G_MonstersSearchingFor (NULL);
	int			i;
	edict_t		*player;

	for (i = 1 ; i <= game.maxclients ; i++)
	{
		player = g_edicts + i;
		if (!player->inuse || !player->client || player->deadflag || player->client->resp.spectator)
			continue;

		// hurt lately, hunted, or shooting at what hunts the rest
		if (player->client->last_damage_time >= level.time
			|| G_MonstersSearchingFor (player)
			|| (searching && player->client->last_firing_time >= level.time))
		{
			player->client->coop_respawn_state = COOP_RESPAWN_IN_COMBAT;
			continue;
		}
		// on the world, out of the water
		if (player->groundentity != g_edicts || player->waterlevel >= 3)
		{
			player->client->coop_respawn_state = COOP_RESPAWN_BAD_AREA;
			continue;
		}
		if (!G_FindRespawnSpot (player, spot))
		{
			player->client->coop_respawn_state = COOP_RESPAWN_BLOCKED;
			continue;
		}
		return player;
	}
	return NULL;
}

/*
=================
G_CoopRespawn

A dead player's way back with squad respawn or lives: beside a living
player, at the start if there is none, or watching meanwhile. False for
1997's, any button.
=================
*/
qboolean G_CoopRespawn (edict_t *ent)
{
	int			i;
	qboolean	alldead;
	edict_t		*good;
	vec3_t		spot;
	enum { RESPAWN_NONE, RESPAWN_SPECTATE, RESPAWN_SQUAD, RESPAWN_START } state;

	if (!coop->value || (!g_coop_squad_respawn->value && !g_coop_enable_lives->value))
		return false;

	state = RESPAWN_NONE;
	if (CoopLives () && !ent->client->pers.lives)
	{
		state = RESPAWN_SPECTATE;
		ent->client->coop_respawn_state = COOP_RESPAWN_NO_LIVES;
	}

	if (state == RESPAWN_NONE)
	{
		if (g_coop_squad_respawn->value)
		{
			alldead = true;
			for (i = 1 ; i <= game.maxclients ; i++)
				if (g_edicts[i].inuse && g_edicts[i].health > 0)
					alldead = false;

			if (alldead)
				state = RESPAWN_START;	// lives left, so to the start
			else if ((good = G_FindSquadRespawnTarget (spot)) != NULL)
			{
				state = RESPAWN_SQUAD;
				VectorCopy (spot, squad_respawn_position);
				VectorCopy (good->s.angles, squad_respawn_angles);
				squad_respawn_angles[ROLL] = 0;
				use_squad_respawn = true;
			}
			else
				state = RESPAWN_SPECTATE;
		}
		else
			state = RESPAWN_START;
	}

	if (state == RESPAWN_SQUAD || state == RESPAWN_START)
	{
		// with instanced items, back with their things and their full health
		if (G_UseCoopInstancedItems ())
			ent->client->pers.health = ent->client->pers.max_health = ent->max_health;
		respawn (ent);
		ent->client->latched_buttons = 0;
		use_squad_respawn = false;
	}
	else if (state == RESPAWN_SPECTATE)
	{
		if (!ent->client->coop_respawn_state)
			ent->client->coop_respawn_state = COOP_RESPAWN_WAITING;

		// watching meanwhile
		if (!ent->client->resp.spectator)
		{
			CopyToBodyQue (ent);
			ent->client->resp.spectator = true;
			ent->solid = SOLID_NOT;
			ent->takedamage = DAMAGE_NO;
			ent->s.modelindex = 0;
			ent->svflags |= SVF_NOCLIENT;
			ent->client->ps.blend[3] = 0;
			ent->client->ps.rdflags = 0;
			ent->movetype = MOVETYPE_NOCLIP;
			gi.linkentity (ent);
			GetChaseTarget (ent);
		}
	}
	return true;
}

/*
=================
G_CoopPlayerDied

A life gone; everyone dead and none left, the level again in five seconds;
otherwise a try at coming back in three
=================
*/
void G_CoopPlayerDied (edict_t *self)
{
	int			i;
	qboolean	alldead;
	edict_t		*p;

	if (!coop->value || (!g_coop_squad_respawn->value && !g_coop_enable_lives->value))
		return;

	if (CoopLives () && self->client->pers.lives)
	{
		self->client->pers.lives--;
		self->client->resp.coop_respawn.lives--;
	}

	alldead = true;
	for (i = 1 ; i <= game.maxclients ; i++)
	{
		p = g_edicts + i;
		if (!p->inuse || !p->client)
			continue;
		if (p->health > 0 || (CoopLives () && p->client->pers.lives > 0))
			alldead = false;
	}

	if (alldead)
	{
		level.coop_level_restart_time = level.time + 5;
		for (i = 1 ; i <= game.maxclients ; i++)
			if (g_edicts[i].inuse)
				gi.centerprintf (g_edicts + i, "$g_coop_lose");
	}

	if (!level.coop_level_restart_time)
		self->client->respawn_time = level.time + 3;
}

static qboolean G_AnyDeadPlayersWithoutLives (void)
{
	int		i;

	for (i = 1 ; i <= game.maxclients ; i++)
		if (g_edicts[i].inuse && g_edicts[i].client
			&& g_edicts[i].health <= 0 && !g_edicts[i].client->pers.lives)
			return true;
	return false;
}

/*
=================
G_CoopFrameBegin

Before the frame's entities: the level again if everyone is dead, and each
player's respawn state set afresh
=================
*/
void G_CoopFrameBegin (void)
{
	int			i;
	edict_t		*p;
	gclient_t	*cl;
	char		userinfo[MAX_INFO_STRING];

	if (level.coop_level_restart_time && level.time > level.coop_level_restart_time)
	{
		level.coop_level_restart_time = 0;

		// each player as they entered the level, with their lives back
		for (i = 1 ; i <= game.maxclients ; i++)
		{
			p = g_edicts + i;
			if (!p->inuse || !p->client)
				continue;
			cl = p->client;
			memcpy (userinfo, cl->pers.userinfo, sizeof(userinfo));
			cl->pers = cl->resp.coop_respawn;
			memcpy (cl->pers.userinfo, userinfo, sizeof(userinfo));
			if (CoopLives ())
				cl->pers.lives = cl->resp.coop_respawn.lives = (int)g_coop_num_lives->value + 1;
			if (cl->pers.health <= 0)
				cl->pers.health = cl->pers.max_health;
			// SpawnEntities takes these back into pers (SaveClientData)
			p->health = cl->pers.health;
			p->max_health = cl->pers.max_health;
			p->flags &= ~FL_POWER_ARMOR;
			p->flags |= cl->pers.savedFlags & FL_POWER_ARMOR;
			cl->resp.spectator = false;
		}
		gi.AddCommandString ("restart_level\n");
		return;
	}

	if (!coop->value || (!g_coop_enable_lives->value && !g_coop_squad_respawn->value))
		return;

	for (i = 1 ; i <= game.maxclients ; i++)
	{
		p = g_edicts + i;
		if (!p->inuse || !p->client)
			continue;
		if (p->client->respawn_time >= level.time)
			p->client->coop_respawn_state = COOP_RESPAWN_WAITING;
		else if (CoopLives () && p->health <= 0 && !p->client->pers.lives)
			p->client->coop_respawn_state = COOP_RESPAWN_NO_LIVES;
		else if (CoopLives () && G_AnyDeadPlayersWithoutLives ())
			p->client->coop_respawn_state = COOP_RESPAWN_NO_LIVES;
		else
			p->client->coop_respawn_state = COOP_RESPAWN_NONE;
	}
}

// after them: none of it shown once everyone is up again
void G_CoopFrameEnd (void)
{
	int		i;

	if (!coop->value || (!g_coop_enable_lives->value && !g_coop_squad_respawn->value))
		return;

	for (i = 1 ; i <= game.maxclients ; i++)
		if (g_edicts[i].inuse && g_edicts[i].health < 0)
			return;
	for (i = 1 ; i <= game.maxclients ; i++)
		if (g_edicts[i].inuse && g_edicts[i].client)
			g_edicts[i].client->coop_respawn_state = COOP_RESPAWN_NONE;
}

/*
=================
G_SetCoopStats

STAT_COOP, which the status bar's "coop" draws (cl_scrn.c): lives and one
more in the low byte, the respawn state above it
=================
*/
void G_SetCoopStats (edict_t *ent)
{
	int		lives = 0;

	if (CoopLives ())
		lives = ent->client->pers.lives + 1;
	ent->client->ps.stats[STAT_COOP] = (lives & 0xff) | (ent->client->coop_respawn_state << 8);
}
