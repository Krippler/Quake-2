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
g_rropts.c -- the 2023 re-release's settings for deathmatch and the rest,
from its game code (g_main.cpp, g_items.cpp, p_client.cpp, p_weapon.cpp,
g_combat.cpp, rogue/g_rogue_newdm.cpp; GPL), with its defaults.

Most of them are 1997's dmflags under names, and G_DMFlags is dmflags with
them in: so either sets each. Some of the re-release's defaults differ from
1997's: instant items, spawning farthest, the quad and DualFire dropped when
their holder dies, and no friendly fire (in co-op too).
*/

#include "g_local.h"

qboolean Pickup_Health (edict_t *ent, edict_t *other);

cvar_t	*g_dm_allow_exit;
cvar_t	*g_dm_force_respawn;
cvar_t	*g_dm_force_respawn_time;
cvar_t	*g_dm_instant_items;
cvar_t	*g_dm_no_fall_damage;
cvar_t	*g_dm_no_quad_drop;
cvar_t	*g_dm_no_quadfire_drop;
cvar_t	*g_dm_no_stack_double;
cvar_t	*g_dm_random_items;
cvar_t	*g_dm_same_level;
cvar_t	*g_dm_spawn_farthest;
cvar_t	*g_dm_weapons_stay;
cvar_t	*g_friendly_fire;
cvar_t	*g_infinite_ammo;
cvar_t	*g_instagib;
cvar_t	*g_instant_weapon_switch;
cvar_t	*g_quick_weapon_switch;
cvar_t	*g_map_list;
cvar_t	*g_map_list_shuffle;
cvar_t	*g_no_armor;
cvar_t	*g_no_health;
cvar_t	*g_no_items;
cvar_t	*g_no_mines;
cvar_t	*g_no_nukes;
cvar_t	*g_no_spheres;
cvar_t	*g_start_items;
cvar_t	*g_damage_scale;
cvar_t	*ai_damage_scale;
cvar_t	*ai_allow_dm_spawn;
cvar_t	*g_weapon_respawn_time;
cvar_t	*teamplay;
cvar_t	*needpass;

void RROptionsInit (void)
{
	g_dm_allow_exit = gi.cvar ("g_dm_allow_exit", "0", 0);
	g_dm_force_respawn = gi.cvar ("g_dm_force_respawn", "0", 0);
	g_dm_force_respawn_time = gi.cvar ("g_dm_force_respawn_time", "0", 0);
	g_dm_instant_items = gi.cvar ("g_dm_instant_items", "1", 0);
	g_dm_no_fall_damage = gi.cvar ("g_dm_no_fall_damage", "0", 0);
	g_dm_no_quad_drop = gi.cvar ("g_dm_no_quad_drop", "0", 0);
	g_dm_no_quadfire_drop = gi.cvar ("g_dm_no_quadfire_drop", "0", 0);
	g_dm_no_stack_double = gi.cvar ("g_dm_no_stack_double", "0", 0);
	g_dm_random_items = gi.cvar ("g_dm_random_items", "0", 0);
	g_dm_same_level = gi.cvar ("g_dm_same_level", "0", 0);
	g_dm_spawn_farthest = gi.cvar ("g_dm_spawn_farthest", "1", 0);
	g_dm_weapons_stay = gi.cvar ("g_dm_weapons_stay", "0", 0);
	g_friendly_fire = gi.cvar ("g_friendly_fire", "0", 0);
	g_infinite_ammo = gi.cvar ("g_infinite_ammo", "0", CVAR_LATCH);
	g_instagib = gi.cvar ("g_instagib", "0", CVAR_SERVERINFO | CVAR_LATCH);
	g_instant_weapon_switch = gi.cvar ("g_instant_weapon_switch", "0", CVAR_LATCH);
	g_quick_weapon_switch = gi.cvar ("g_quick_weapon_switch", "1", CVAR_LATCH);
	g_map_list = gi.cvar ("g_map_list", "", 0);
	g_map_list_shuffle = gi.cvar ("g_map_list_shuffle", "0", 0);
	g_no_armor = gi.cvar ("g_no_armor", "0", 0);
	g_no_health = gi.cvar ("g_no_health", "0", 0);
	g_no_items = gi.cvar ("g_no_items", "0", 0);
	g_no_mines = gi.cvar ("g_no_mines", "0", 0);
	g_no_nukes = gi.cvar ("g_no_nukes", "0", 0);
	g_no_spheres = gi.cvar ("g_no_spheres", "0", 0);
	g_start_items = gi.cvar ("g_start_items", "", CVAR_LATCH);
	g_damage_scale = gi.cvar ("g_damage_scale", "1", 0);
	ai_damage_scale = gi.cvar ("ai_damage_scale", "1", 0);
	ai_allow_dm_spawn = gi.cvar ("ai_allow_dm_spawn", "0", 0);
	g_weapon_respawn_time = gi.cvar ("g_weapon_respawn_time", "30", 0);
	teamplay = gi.cvar ("teamplay", "0", CVAR_SERVERINFO | CVAR_LATCH);
	needpass = gi.cvar ("needpass", "0", CVAR_SERVERINFO);

	// team deathmatch is deathmatch's, and not CTF's
	if (teamplay->value && (!deathmatch->value || ctf->value))
		gi.cvar_forceset ("teamplay", "0");
}

/*
=================
G_DMFlags

dmflags, with the re-release's settings for them in
=================
*/
int G_DMFlags (void)
{
	int		f = (int)dmflags->value;

	if (g_dm_weapons_stay->value)		f |= DF_WEAPONS_STAY;
	if (g_dm_instant_items->value)		f |= DF_INSTANT_ITEMS;
	if (g_dm_no_fall_damage->value)		f |= DF_NO_FALLING;
	if (g_dm_same_level->value)			f |= DF_SAME_LEVEL;
	if (g_dm_spawn_farthest->value)		f |= DF_SPAWN_FARTHEST;
	if (g_dm_force_respawn->value)		f |= DF_FORCE_RESPAWN;
	if (g_dm_allow_exit->value)			f |= DF_ALLOW_EXIT;
	if (g_no_armor->value)				f |= DF_NO_ARMOR;
	if (g_no_health->value)				f |= DF_NO_HEALTH;
	if (g_no_items->value)				f |= DF_NO_ITEMS;
	if (g_no_mines->value)				f |= DF_NO_MINES;
	if (g_no_nukes->value)				f |= DF_NO_NUKES;
	if (g_no_spheres->value)			f |= DF_NO_SPHERES;
	if (g_dm_no_stack_double->value)	f |= DF_NO_STACK_DOUBLE;
	if (g_infinite_ammo->value || G_Instagib ())
		f |= DF_INFINITE_AMMO;
	// dropped unless asked not to, the other way to 1997's
	if (!g_dm_no_quad_drop->value)		f |= DF_QUAD_DROP;
	if (!g_dm_no_quadfire_drop->value)	f |= DF_QUADFIRE_DROP;
	// none unless asked for
	if (!g_friendly_fire->value)		f |= DF_NO_FRIENDLY_FIRE;
	return f;
}

qboolean G_Instagib (void)
{
	return deathmatch->value && g_instagib->value;
}

// team deathmatch, or CTF: teams either way
qboolean G_TeamplayEnabled (void)
{
	return ctf->value || teamplay->value;
}

/*
=================
G_GiveStartItems

"classname count;classname count;...": each picked up, as a dropped one
would be (the re-release's Player_GiveStartItems). 0 takes it away.
=================
*/
void G_GiveStartItems (edict_t *ent, char *list)
{
	char	buf[MAX_STRING_CHARS], *s, *item_s, *t;
	gitem_t	*item;
	edict_t	*dummy;
	int		count;

	Com_sprintf (buf, sizeof(buf), "%s", list);
	for (s = buf ; s && *s ; s = t)
	{
		t = strchr (s, ';');
		if (t)
			*t++ = 0;

		item_s = COM_Parse (&s);
		if (!item_s[0])
			continue;
		item = FindItemByClassname (item_s);
		if (!item || !item->pickup)
		{
			gi.dprintf ("Invalid start item: %s\n", item_s);
			continue;
		}
		count = 1;
		if (s && *s)
			count = atoi (COM_Parse (&s));

		if (!count)
		{
			ent->client->pers.inventory[ITEM_INDEX(item)] = 0;
			continue;
		}

		dummy = G_Spawn ();
		dummy->classname = item->classname;
		dummy->item = item;
		dummy->count = count;
		dummy->spawnflags |= DROPPED_ITEM;
		item->pickup (dummy, ent);
		if (dummy->inuse)
			G_FreeEdict (dummy);
	}
}

/*
=================
G_RandomItem

Ground Zero's random items in deathmatch (g_dm_random_items, its
rogue/g_rogue_newdm.cpp): another of its kind for the one there, or NULL to
keep it
=================
*/
static int G_RandomItemKind (gitem_t *it)
{
	int		f = it->flags & (IT_WEAPON | IT_AMMO | IT_POWERUP | IT_ARMOR | IT_KEY);

	if ((f & (IT_WEAPON | IT_AMMO)) == (IT_WEAPON | IT_AMMO))
		f = IT_AMMO;
	else if (!strcmp (it->classname, "item_adrenaline") || !strcmp (it->classname, "item_health_mega"))
		f = IT_POWERUP;
	return f;
}

gitem_t *G_RandomItem (gitem_t *item)
{
	static const char	*armors[] = { "item_armor_jacket", "item_armor_combat",
		"item_armor_body", "item_power_screen", "item_power_shield" };
	gitem_t	*possible[MAX_ITEMS], *it;
	int		i, n, kind;
	float	r;

	// flags, the token, and health, which is a size of its one item here
	// rather than items of its own; a shard, among the armour, stays one
	if (!item || !strcmp (item->classname, "item_flag_team1") || !strcmp (item->classname, "item_flag_team2")
		|| !strcmp (item->classname, "item_tag_token") || item->pickup == Pickup_Health
		|| !strcmp (item->classname, "item_armor_shard"))
		return NULL;

	for (i = 0 ; i < 5 ; i++)
		if (!strcmp (item->classname, armors[i]))
		{
			r = random ();
			return FindItemByClassname ((char *)armors[r < 0.4 ? 0 : r < 0.6 ? 1 : r < 0.8 ? 2 : r < 0.9 ? 3 : 4]);
		}

	kind = G_RandomItemKind (item);
	for (i = 1, n = 0 ; i < game.num_items ; i++)
	{
		it = &itemlist[i];
		if (!it->flags || (it->flags & (IT_TECH | IT_KEY)) || !it->pickup || !it->world_model)
			continue;
		if (!strcmp (it->classname, "weapon_grapple") || !strcmp (it->classname, "item_compass")
			|| !strcmp (it->classname, "item_tag_token"))
			continue;
		if (g_no_spheres->value && (!strcmp (it->classname, "item_sphere_vengeance")
			|| !strcmp (it->classname, "item_sphere_hunter") || !strcmp (it->classname, "item_sphere_defender")))
			continue;
		if (g_no_nukes->value && !strcmp (it->classname, "ammo_nuke"))
			continue;
		if (g_no_mines->value && (!strcmp (it->classname, "ammo_prox") || !strcmp (it->classname, "ammo_tesla")
			|| !strcmp (it->classname, "ammo_trap") || !strcmp (it->classname, "weapon_proxlauncher")))
			continue;
		if (G_RandomItemKind (it) == kind)
			possible[n++] = it;
	}
	if (!n)
		return NULL;
	return possible[rand () % n];
}

/*
=================
G_NextMapInList

The map after this one in g_map_list (the first after the last, shuffled
anew if g_map_list_shuffle says), or NULL if this one is not in it
=================
*/
char *G_NextMapInList (void)
{
	static char	next[MAX_QPATH];
	char		list[MAX_STRING_CHARS], *maps[256], *s, *t;
	int			n, i, j;

	if (!g_map_list->string[0])
		return NULL;

	Com_sprintf (list, sizeof(list), "%s", g_map_list->string);
	for (n = 0, s = list ; n < 256 ; )
	{
		t = COM_Parse (&s);
		if (!t[0])
			break;
		maps[n++] = strcpy (gi.TagMalloc (strlen (t) + 1, TAG_LEVEL), t);
		if (!s)
			break;
	}
	for (i = 0 ; i < n ; i++)
		if (!Q_stricmp (maps[i], level.mapname))
			break;
	if (i == n)
		return n ? maps[0] : NULL;		// not in it: its first

	if (i + 1 < n)
	{
		Com_sprintf (next, sizeof(next), "%s", maps[i + 1]);
		return next;
	}

	// the end: round again, shuffled first if asked
	if (g_map_list_shuffle->value && n > 1)
	{
		char	*tmp, shuffled[MAX_STRING_CHARS];

		for (i = n - 1 ; i > 0 ; i--)
		{
			j = rand () % (i + 1);
			tmp = maps[i];
			maps[i] = maps[j];
			maps[j] = tmp;
		}
		// not this one again at once
		if (!Q_stricmp (maps[0], level.mapname))
		{
			tmp = maps[0];
			maps[0] = maps[n - 1];
			maps[n - 1] = tmp;
		}
		shuffled[0] = 0;
		for (i = 0 ; i < n ; i++)
		{
			strncat (shuffled, maps[i], sizeof(shuffled) - strlen (shuffled) - 2);
			strcat (shuffled, " ");
		}
		gi.cvar_forceset ("g_map_list", shuffled);
	}
	Com_sprintf (next, sizeof(next), "%s", maps[0]);
	return next;
}

/*
=================
CheckNeedPass

needpass, for the server's browsers: 1 for a password to play, 2 to
spectate (Quake II 3.20's and the re-release's)
=================
*/
void CheckNeedPass (void)
{
	int		need;

	if (password->modified || spectator_password->modified)
	{
		password->modified = spectator_password->modified = false;
		need = 0;
		if (*password->string && Q_stricmp (password->string, "none"))
			need |= 1;
		if (*spectator_password->string && Q_stricmp (spectator_password->string, "none"))
			need |= 2;
		gi.cvar_set ("needpass", va ("%d", need));
	}
}
