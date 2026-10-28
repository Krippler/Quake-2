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
// g_weapon.c

#include "g_local.h"
#include "m_player.h"


qboolean	is_quad;
static qboolean	is_quadfire;		// The Reckoning's DualFire
static int		damage_multiplier = 1;	// quad's 4, and Ground Zero's double damage 2
static byte		is_silenced;


void weapon_grenade_fire (edict_t *ent, qboolean held);


/*
=================
P_ProjectSource

Where a shot leaves the gun. On the re-release's maps it goes, as there,
not straight along the view from the side of it but toward whatever the
crosshair is on, so it hits what is aimed at however near; forward is
turned that way. Something close in front, a monster or a player within
128 units, or a gun in a wall, is shot straight ahead, so a railgun
pierces what is in front of it.
=================
*/
void P_ProjectSource (gclient_t *client, vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
	vec3_t	_distance, eye, end;
	edict_t	*ent;
	trace_t	tr;

	VectorCopy (distance, _distance);
	if (client->pers.hand == LEFT_HANDED)
		_distance[1] *= -1;
	else if (client->pers.hand == CENTER_HANDED)
		_distance[1] = 0;
	G_ProjectSource (point, _distance, forward, right, result);

	if (!RR_Map ())
		return;

	ent = g_edicts + 1 + (client - game.clients);
	VectorCopy (ent->s.origin, eye);
	eye[2] += ent->viewheight;
	VectorMA (eye, 8192, forward, end);
	tr = gi.trace (eye, NULL, NULL, end, ent, MASK_SHOT & ~CONTENTS_DEADMONSTER);
	if (tr.startsolid || (tr.ent && ((tr.ent->svflags & SVF_MONSTER) || tr.ent->client)
		&& tr.fraction * 8192 < 128))
		return;
	VectorSubtract (tr.endpos, result, forward);
	VectorNormalize (forward);
}

/*
=================
P_RemoveAmmo

A shot's ammo, and on the re-release's maps its warning as the weapon's
ammo runs low: when it falls to its quantity_warn, or for a weapon
without one, to none.
=================
*/
static int P_QuantityWarn (gitem_t *weapon)
{
	static struct { char *classname; int warn; } warns[] = {
		{"weapon_supershotgun", 10}, {"weapon_machinegun", 30},
		{"weapon_chaingun", 60}, {"ammo_grenades", 2}, {"ammo_tesla", 1},
		{"weapon_hyperblaster", 30}, {"weapon_plasmabeam", 50},
		{"weapon_bfg", 50}, {"weapon_boomer", 30}, {"ammo_trap", 1}, {"weapon_etf_rifle", 30}, {NULL, 0}
	};
	int		i;

	for (i=0 ; warns[i].classname ; i++)
		if (!strcmp (weapon->classname, warns[i].classname))
			return warns[i].warn;
	return 0;
}

static void P_RemoveAmmo (edict_t *ent, int count)
{
	int			*ammo = &ent->client->pers.inventory[ent->client->ammo_index];
	int			warn;
	qboolean	warned;

	if (!RR_Map () || !ent->client->pers.weapon)
	{
		*ammo -= count;
		return;
	}
	warn = P_QuantityWarn (ent->client->pers.weapon);
	warned = *ammo <= warn;
	*ammo -= count;
	if (!warned && *ammo <= warn)
		gi.sound (ent, CHAN_AUTO, gi.soundindex ("weapons/lowammo.wav"), 1, ATTN_NORM, 0);
}


/*
===============
PlayerNoise

Each player can have two noise objects associated with it:
a personal noise (jumping, pain, weapon firing), and a weapon
target noise (bullet wall impacts)

Monsters that don't directly see the player can move
to a noise in hopes of seeing the player from there.
===============
*/
void PlayerNoise(edict_t *who, vec3_t where, int type)
{
	edict_t		*noise;

	if (type == PNOISE_WEAPON)
	{
		// an invisible player's shot shows them, for two seconds (less
		// with the silencer)
		if (who->client)
			who->client->invisibility_fade_framenum = level.framenum
				+ (who->client->silencer_shots ? 4 : 20);
		if (who->client->silencer_shots)
		{
			who->client->silencer_shots--;
			return;
		}
	}

	if (deathmatch->value)
		return;

	if (who->flags & FL_NOTARGET)
		return;

	// Ground Zero's: a disguised player is heard only firing, and known for it
	if (who->flags & FL_DISGUISED)
	{
		if (type != PNOISE_WEAPON)
			return;
		level.disguise_violator = who;
		level.disguise_violation_framenum = level.framenum + 5;
	}

	if (!who->mynoise)
	{
		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet (noise->mins, -8, -8, -8);
		VectorSet (noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise = noise;

		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet (noise->mins, -8, -8, -8);
		VectorSet (noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise2 = noise;
	}

	if (type == PNOISE_SELF || type == PNOISE_WEAPON)
	{
		noise = who->mynoise;
		level.sound_entity = noise;
		level.sound_entity_framenum = level.framenum;
	}
	else // type == PNOISE_IMPACT
	{
		noise = who->mynoise2;
		level.sound2_entity = noise;
		level.sound2_entity_framenum = level.framenum;
	}

	VectorCopy (where, noise->s.origin);
	VectorSubtract (where, noise->maxs, noise->absmin);
	VectorAdd (where, noise->maxs, noise->absmax);
	noise->teleport_time = level.time;
	gi.linkentity (noise);
}


qboolean Pickup_Weapon (edict_t *ent, edict_t *other)
{
	int			index;
	gitem_t		*ammo;

	index = ITEM_INDEX(ent->item);

	if ( ( ((int)(dmflags->value) & DF_WEAPONS_STAY) || coop->value) 
		&& other->client->pers.inventory[index])
	{
		if (!(ent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM) ) )
			return false;	// leave the weapon for others to pickup
	}

	other->client->pers.inventory[index]++;

	if (!(ent->spawnflags & DROPPED_ITEM) )
	{
		// give them some ammo with it
		ammo = FindItem (ent->item->ammo);
		if ( (int)dmflags->value & DF_INFINITE_AMMO )
			Add_Ammo (other, ammo, 1000);
		else
			Add_Ammo (other, ammo, ammo->quantity);

		if (! (ent->spawnflags & DROPPED_PLAYER_ITEM) )
		{
			if (deathmatch->value)
			{
				if ((int)(dmflags->value) & DF_WEAPONS_STAY)
					ent->flags |= FL_RESPAWN;
				else
					SetRespawn (ent, 30);
			}
			if (coop->value)
				ent->flags |= FL_RESPAWN;
		}
	}

	if (other->client->pers.weapon != ent->item && 
		(other->client->pers.inventory[index] == 1) &&
		( !deathmatch->value || other->client->pers.weapon == FindItem("blaster") ) )
		other->client->newweapon = ent->item;

	return true;
}


/*
===============
ChangeWeapon

The old weapon has been dropped all the way, so make the new one
current
===============
*/
void ChangeWeapon (edict_t *ent)
{
	int i;

	if (ent->client->grenade_time)
	{
		ent->client->grenade_time = level.time;
		ent->client->weapon_sound = 0;
		weapon_grenade_fire (ent, false);
		ent->client->grenade_time = 0;
	}

	// the re-release's sound of a weapon being changed for another
	if (RR_Map () && ent->client->pers.weapon && ent->client->newweapon
		&& ent->client->newweapon != ent->client->pers.weapon)
		gi.sound (ent, CHAN_WEAPON, gi.soundindex ("weapons/change.wav"), 1, ATTN_NORM, 0);

	ent->client->pers.lastweapon = ent->client->pers.weapon;
	ent->client->pers.weapon = ent->client->newweapon;
	ent->client->newweapon = NULL;
	ent->client->machinegun_shots = 0;

	// set visible model
	if (ent->s.modelindex == 255) {
		if (ent->client->pers.weapon)
			i = ((ent->client->pers.weapon->weapmodel & 0xff) << 8);
		else
			i = 0;
		ent->s.skinnum = (ent - g_edicts - 1) | i;
	}

	if (ent->client->pers.weapon && ent->client->pers.weapon->ammo)
		ent->client->ammo_index = ITEM_INDEX(FindItem(ent->client->pers.weapon->ammo));
	else
		ent->client->ammo_index = 0;

	if (!ent->client->pers.weapon)
	{	// dead
		ent->client->ps.gunindex = 0;
		return;
	}

	ent->client->weaponstate = WEAPON_ACTIVATING;
	ent->client->ps.gunframe = 0;
	ent->client->ps.gunindex = gi.modelindex(ent->client->pers.weapon->view_model);

	ent->client->anim_priority = ANIM_PAIN;
	if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
			ent->s.frame = FRAME_crpain1;
			ent->client->anim_end = FRAME_crpain4;
	}
	else
	{
			ent->s.frame = FRAME_pain301;
			ent->client->anim_end = FRAME_pain304;
			
	}
}

/*
=================
NoAmmoWeaponChange
=================
*/
void NoAmmoWeaponChange (edict_t *ent)
{
	// the re-release's order, which takes up launchers and the mission
	// packs' weapons too, rather than 1997's blaster
	if (RR_Map ())
	{
		static char *order[] = {
			"weapon_disintegrator", "weapon_railgun", "weapon_plasmabeam",
			"weapon_boomer", "weapon_hyperblaster", "weapon_etf_rifle", "weapon_chaingun", "weapon_machinegun",
			"weapon_supershotgun", "weapon_shotgun", "weapon_phalanx",
			"weapon_rocketlauncher", "weapon_grenadelauncher", "weapon_proxlauncher",
			"weapon_chainfist", "weapon_blaster", NULL
		};
		gitem_t	*item, *ammo;
		int		i;

		for (i=0 ; order[i] ; i++)
		{
			item = FindItemByClassname (order[i]);
			if (!item || !ent->client->pers.inventory[ITEM_INDEX(item)])
				continue;
			if (item->ammo)
			{
				ammo = FindItem (item->ammo);
				if (!ammo || ent->client->pers.inventory[ITEM_INDEX(ammo)] < item->quantity)
					continue;
			}
			ent->client->newweapon = item;
			return;
		}
	}

	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("slugs"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("railgun"))] )
	{
		ent->client->newweapon = FindItem ("railgun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("cells"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("hyperblaster"))] )
	{
		ent->client->newweapon = FindItem ("hyperblaster");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("bullets"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("chaingun"))] )
	{
		ent->client->newweapon = FindItem ("chaingun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("bullets"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("machinegun"))] )
	{
		ent->client->newweapon = FindItem ("machinegun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("shells"))] > 1
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("super shotgun"))] )
	{
		ent->client->newweapon = FindItem ("super shotgun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("shells"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("shotgun"))] )
	{
		ent->client->newweapon = FindItem ("shotgun");
		return;
	}
	ent->client->newweapon = FindItem ("blaster");
}

/*
=================
Think_Weapon

Called by ClientBeginServerFrame and ClientThink
=================
*/
void Think_Weapon (edict_t *ent)
{
	// if just died, put the weapon away
	if (ent->health < 1)
	{
		ent->client->newweapon = NULL;
		ChangeWeapon (ent);
	}

	// call active weapon think routine
	if (ent->client->pers.weapon && ent->client->pers.weapon->weaponthink)
	{
		is_quad = (ent->client->quad_framenum > level.framenum);
		is_quadfire = (ent->client->quadfire_framenum > level.framenum);
		// Ground Zero's: double damage doubles quad's only in deathmatch
		damage_multiplier = is_quad ? 4 : 1;
		if (ent->client->double_framenum > level.framenum
			&& (deathmatch->value || damage_multiplier == 1))
			damage_multiplier *= 2;
		if (ent->client->silencer_shots)
			is_silenced = MZ_SILENCED;
		else
			is_silenced = 0;
		ent->client->pers.weapon->weaponthink (ent);
	}
}


/*
================
Use_Weapon

Make the weapon ready if there is ammo
================
*/
void Use_Weapon (edict_t *ent, gitem_t *item)
{
	int			ammo_index;
	gitem_t		*ammo_item;

	// see if we're already using it
	if (item == ent->client->pers.weapon)
		return;

	if (item->ammo && !g_select_empty->value && !(item->flags & IT_AMMO))
	{
		ammo_item = FindItem(item->ammo);
		ammo_index = ITEM_INDEX(ammo_item);

		if (!ent->client->pers.inventory[ammo_index])
		{
			gi.cprintf (ent, PRINT_HIGH, "No %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
			return;
		}

		if (ent->client->pers.inventory[ammo_index] < item->quantity)
		{
			gi.cprintf (ent, PRINT_HIGH, "Not enough %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
			return;
		}
	}

	// change to this weapon when down
	ent->client->newweapon = item;
}



/*
================
Drop_Weapon
================
*/
void Drop_Weapon (edict_t *ent, gitem_t *item)
{
	int		index;

	if ((int)(dmflags->value) & DF_WEAPONS_STAY)
		return;

	index = ITEM_INDEX(item);
	// see if we're already using it
	if ( ((item == ent->client->pers.weapon) || (item == ent->client->newweapon))&& (ent->client->pers.inventory[index] == 1) )
	{
		gi.cprintf (ent, PRINT_HIGH, "Can't drop current weapon\n");
		return;
	}

	Drop_Item (ent, item);
	ent->client->pers.inventory[index]--;
}


/*
================
Weapon_Generic

A generic function to handle the basics of weapon thinking
================
*/
#define FRAME_FIRE_FIRST		(FRAME_ACTIVATE_LAST + 1)
#define FRAME_IDLE_FIRST		(FRAME_FIRE_LAST + 1)
#define FRAME_DEACTIVATE_FIRST	(FRAME_IDLE_LAST + 1)

static void Weapon_Generic2 (edict_t *ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, int *pause_frames, int *fire_frames, void (*fire)(edict_t *ent));

// The Reckoning's DualFire runs a weapon's frames twice a frame
void Weapon_Generic (edict_t *ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, int *pause_frames, int *fire_frames, void (*fire)(edict_t *ent))
{
	gitem_t	*weapon = ent->client->pers.weapon;

	int		oldstate = ent->client->weaponstate;

	Weapon_Generic2 (ent, FRAME_ACTIVATE_LAST, FRAME_FIRE_LAST, FRAME_IDLE_LAST, FRAME_DEACTIVATE_LAST, pause_frames, fire_frames, fire);
	// (not if that changed weapons)
	if (!ent->inuse || ent->client->pers.weapon != weapon)
		return;
	if (is_quadfire)
	{
		Weapon_Generic2 (ent, FRAME_ACTIVATE_LAST, FRAME_FIRE_LAST, FRAME_IDLE_LAST, FRAME_DEACTIVATE_LAST, pause_frames, fire_frames, fire);
		return;
	}

	// CTF's: the frame run again with the haste tech, or for the grapple
	// but while it fires
	if (weapon && !Q_stricmp (weapon->pickup_name, "Grapple") && ent->client->weaponstate == WEAPON_FIRING)
		return;
	if ((CTFApplyHaste (ent) || (weapon && !Q_stricmp (weapon->pickup_name, "Grapple")
		&& ent->client->weaponstate != WEAPON_FIRING))
		&& oldstate == ent->client->weaponstate)
		Weapon_Generic2 (ent, FRAME_ACTIVATE_LAST, FRAME_FIRE_LAST, FRAME_IDLE_LAST, FRAME_DEACTIVATE_LAST, pause_frames, fire_frames, fire);
}

static void Weapon_Generic2 (edict_t *ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, int *pause_frames, int *fire_frames, void (*fire)(edict_t *ent))
{
	int		n;

	if(ent->deadflag || ent->s.modelindex != 255) // VWep animations screw up corpses
	{
		return;
	}

	if (ent->client->weaponstate == WEAPON_DROPPING)
	{
		if (ent->client->ps.gunframe == FRAME_DEACTIVATE_LAST)
		{
			ChangeWeapon (ent);
			return;
		}
		else if ((FRAME_DEACTIVATE_LAST - ent->client->ps.gunframe) == 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4+1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304+1;
				ent->client->anim_end = FRAME_pain301;
				
			}
		}

		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		if (ent->client->ps.gunframe == FRAME_ACTIVATE_LAST || instantweap->value)
		{
			ent->client->weaponstate = WEAPON_READY;
			ent->client->ps.gunframe = FRAME_IDLE_FIRST;
			// CTF's instantweap: ready at once, and run as ready
			if (instantweap->value)
				Weapon_Generic2 (ent, FRAME_ACTIVATE_LAST, FRAME_FIRE_LAST,
					FRAME_IDLE_LAST, FRAME_DEACTIVATE_LAST, pause_frames,
					fire_frames, fire);
			return;
		}

		ent->client->ps.gunframe++;
		return;
	}

	if ((ent->client->newweapon) && (ent->client->weaponstate != WEAPON_FIRING))
	{
		ent->client->weaponstate = WEAPON_DROPPING;
		if (instantweap->value)		// CTF's: changed at once
		{
			ChangeWeapon (ent);
			return;
		}
		ent->client->ps.gunframe = FRAME_DEACTIVATE_FIRST;

		if ((FRAME_DEACTIVATE_LAST - FRAME_DEACTIVATE_FIRST) < 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4+1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304+1;
				ent->client->anim_end = FRAME_pain301;
				
			}
		}
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ( ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK) )
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			if ((!ent->client->ammo_index) || 
				( ent->client->pers.inventory[ent->client->ammo_index] >= ent->client->pers.weapon->quantity))
			{
				ent->client->ps.gunframe = FRAME_FIRE_FIRST;
				ent->client->weaponstate = WEAPON_FIRING;

				// start the animation
				ent->client->anim_priority = ANIM_ATTACK;
				if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
				{
					ent->s.frame = FRAME_crattak1-1;
					ent->client->anim_end = FRAME_crattak9;
				}
				else
				{
					ent->s.frame = FRAME_attack1-1;
					ent->client->anim_end = FRAME_attack8;
				}
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}
				NoAmmoWeaponChange (ent);
			}
		}
		else
		{
			if (ent->client->ps.gunframe == FRAME_IDLE_LAST)
			{
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
				return;
			}

			if (pause_frames)
			{
				for (n = 0; pause_frames[n]; n++)
				{
					if (ent->client->ps.gunframe == pause_frames[n])
					{
						if (rand()&15)
							return;
					}
				}
			}

			ent->client->ps.gunframe++;
			return;
		}
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		for (n = 0; fire_frames[n]; n++)
		{
			if (ent->client->ps.gunframe == fire_frames[n])
			{
				// CTF's strength tech's sound, or the quad's
				if (CTFApplyStrengthSound (ent))
					;
				else if (ent->client->quad_framenum > level.framenum)
					gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);
				else if (ent->client->double_framenum > level.framenum)
					gi.sound(ent, CHAN_ITEM, gi.soundindex("misc/ddamage3.wav"), 1, ATTN_NORM, 0);
				CTFApplyHasteSound (ent);

				fire (ent);
				break;
			}
		}

		if (!fire_frames[n])
			ent->client->ps.gunframe++;

		if (ent->client->ps.gunframe == FRAME_IDLE_FIRST+1)
			ent->client->weaponstate = WEAPON_READY;
	}
}


/*
======================================================================

GRENADE

======================================================================
*/

#define GRENADE_TIMER		3.0
#define GRENADE_MINSPEED	400
#define GRENADE_MAXSPEED	800

void weapon_grenade_fire (edict_t *ent, qboolean held)
{
	vec3_t	offset;
	vec3_t	forward, right;
	vec3_t	start;
	int		damage = 125;
	float	timer;
	int		speed;
	float	radius;

	radius = damage+40;
	damage *= damage_multiplier;

	VectorSet(offset, 8, 8, ent->viewheight-8);
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	if (RR_Map ())
	{
		// thrown from low in the middle, and not so far up as to go
		// behind
		vec3_t	angles;

		VectorCopy (ent->client->v_angle, angles);
		if (angles[PITCH] < -62.5)
			angles[PITCH] = -62.5;
		AngleVectors (angles, forward, right, NULL);
		VectorSet(offset, 2, 0, ent->viewheight-14);
	}
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	timer = ent->client->grenade_time - level.time;
	speed = GRENADE_MINSPEED + (GRENADE_TIMER - timer) * ((GRENADE_MAXSPEED - GRENADE_MINSPEED) / GRENADE_TIMER);
	fire_grenade2 (ent, start, forward, damage, speed, timer, radius, held);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 1);

	ent->client->grenade_time = level.time + 1.0;

	if(ent->deadflag || ent->s.modelindex != 255) // VWep animations screw up corpses
	{
		return;
	}

	if (ent->health <= 0)
		return;

	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->client->anim_priority = ANIM_ATTACK;
		ent->s.frame = FRAME_crattak1-1;
		ent->client->anim_end = FRAME_crattak3;
	}
	else
	{
		ent->client->anim_priority = ANIM_REVERSE;
		ent->s.frame = FRAME_wave08;
		ent->client->anim_end = FRAME_wave01;
	}
}

void Weapon_Grenade (edict_t *ent)
{
	if ((ent->client->newweapon) && (ent->client->weaponstate == WEAPON_READY))
	{
		ChangeWeapon (ent);
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		ent->client->weaponstate = WEAPON_READY;
		ent->client->ps.gunframe = 16;
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ( ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK) )
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			if (ent->client->pers.inventory[ent->client->ammo_index])
			{
				ent->client->ps.gunframe = 1;
				ent->client->weaponstate = WEAPON_FIRING;
				ent->client->grenade_time = 0;
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}
				NoAmmoWeaponChange (ent);
			}
			return;
		}

		if ((ent->client->ps.gunframe == 29) || (ent->client->ps.gunframe == 34) || (ent->client->ps.gunframe == 39) || (ent->client->ps.gunframe == 48))
		{
			if (rand()&15)
				return;
		}

		if (++ent->client->ps.gunframe > 48)
			ent->client->ps.gunframe = 16;
		return;
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		if (ent->client->ps.gunframe == 5)
			gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/hgrena1b.wav"), 1, ATTN_NORM, 0);

		if (ent->client->ps.gunframe == 11)
		{
			if (!ent->client->grenade_time)
			{
				ent->client->grenade_time = level.time + GRENADE_TIMER + 0.2;
				ent->client->weapon_sound = gi.soundindex("weapons/hgrenc1b.wav");
			}

			// they waited too long, detonate it in their hand
			if (!ent->client->grenade_blew_up && level.time >= ent->client->grenade_time)
			{
				ent->client->weapon_sound = 0;
				weapon_grenade_fire (ent, true);
				ent->client->grenade_blew_up = true;
			}

			if (ent->client->buttons & BUTTON_ATTACK)
				return;

			if (ent->client->grenade_blew_up)
			{
				if (level.time >= ent->client->grenade_time)
				{
					ent->client->ps.gunframe = 15;
					ent->client->grenade_blew_up = false;
				}
				else
				{
					return;
				}
			}
		}

		if (ent->client->ps.gunframe == 12)
		{
			ent->client->weapon_sound = 0;
			weapon_grenade_fire (ent, false);
		}

		if ((ent->client->ps.gunframe == 15) && (level.time < ent->client->grenade_time))
			return;

		ent->client->ps.gunframe++;

		if (ent->client->ps.gunframe == 16)
		{
			ent->client->grenade_time = 0;
			ent->client->weaponstate = WEAPON_READY;
		}
	}
}

/*
======================================================================

GRENADE LAUNCHER

======================================================================
*/

void weapon_grenadelauncher_fire (edict_t *ent)
{
	vec3_t	offset;
	vec3_t	forward, right;
	vec3_t	start;
	int		damage = 120;
	float	radius;

	radius = damage+40;
	damage *= damage_multiplier;

	VectorSet(offset, 8, 8, ent->viewheight-8);
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	if (RR_Map ())
	{
		// from the middle, and not so far up as to go behind
		vec3_t	angles;

		VectorCopy (ent->client->v_angle, angles);
		if (angles[PITCH] < -62.5)
			angles[PITCH] = -62.5;
		AngleVectors (angles, forward, right, NULL);
		VectorSet(offset, 8, 0, ent->viewheight-8);
	}
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	fire_grenade (ent, start, forward, damage, 600, 2.5, radius);

	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_GRENADE | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 1);
}

void Weapon_GrenadeLauncher (edict_t *ent)
{
	static int	pause_frames[]	= {34, 51, 59, 0};
	static int	fire_frames[]	= {6, 0};

	Weapon_Generic (ent, 5, 16, 59, 64, pause_frames, fire_frames, weapon_grenadelauncher_fire);
}

/*
======================================================================

ROCKET

======================================================================
*/

void Weapon_RocketLauncher_Fire (edict_t *ent)
{
	vec3_t	offset, start;
	vec3_t	forward, right;
	int		damage;
	float	damage_radius;
	int		radius_damage;

	damage = 100 + (int)(random() * 20.0);
	radius_damage = 120;
	damage_radius = 120;
	damage *= damage_multiplier;
	radius_damage *= damage_multiplier;

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	VectorSet(offset, 8, 8, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_rocket (ent, start, forward, damage, 650, damage_radius, radius_damage);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_ROCKET | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 1);
}

void Weapon_RocketLauncher (edict_t *ent)
{
	static int	pause_frames[]	= {25, 33, 42, 50, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 12, 50, 54, pause_frames, fire_frames, Weapon_RocketLauncher_Fire);
}


/*
======================================================================

BLASTER / HYPERBLASTER

======================================================================
*/

void Blaster_Fire (edict_t *ent, vec3_t g_offset, int damage, qboolean hyper, int effect)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	offset;

	damage *= damage_multiplier;
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	VectorSet(offset, 24, 8, ent->viewheight-8);
	VectorAdd (offset, g_offset, offset);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;
	// the re-release's hyperblaster shakes the view, and its blaster's
	// bolts are faster, "because it is a completely useless gun"
	if (hyper && RR_Map ())
	{
		ent->client->kick_angles[0] = crandom() * 0.7;
		ent->client->kick_angles[1] = crandom() * 0.7;
		ent->client->kick_angles[2] = crandom() * 0.7;
	}

	fire_blaster (ent, start, forward, damage, (!hyper && RR_Map ()) ? 1500 : 1000, effect, hyper);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	if (hyper)
		gi.WriteByte (MZ_HYPERBLASTER | is_silenced);
	else
		gi.WriteByte (MZ_BLASTER | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);
}


void Weapon_Blaster_Fire (edict_t *ent)
{
	int		damage;

	// the re-release's is 15 everywhere, not only in deathmatch
	if (deathmatch->value || RR_Map ())
		damage = 15;
	else
		damage = 10;
	Blaster_Fire (ent, vec3_origin, damage, false, EF_BLASTER);
	ent->client->ps.gunframe++;
}

void Weapon_Blaster (edict_t *ent)
{
	static int	pause_frames[]	= {19, 32, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 8, 52, 55, pause_frames, fire_frames, Weapon_Blaster_Fire);
}


void Weapon_HyperBlaster_Fire (edict_t *ent)
{
	float	rotation;
	vec3_t	offset;
	int		effect;
	int		damage;

	ent->client->weapon_sound = gi.soundindex("weapons/hyprbl1a.wav");

	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe++;
	}
	else
	{
		if (! ent->client->pers.inventory[ent->client->ammo_index] )
		{
			if (level.time >= ent->pain_debounce_time)
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
				ent->pain_debounce_time = level.time + 1;
			}
			NoAmmoWeaponChange (ent);
		}
		else
		{
			rotation = (ent->client->ps.gunframe - 5) * 2*M_PI/6;
			offset[0] = -4 * sin(rotation);
			offset[1] = 0;
			offset[2] = 4 * cos(rotation);

			if ((ent->client->ps.gunframe == 6) || (ent->client->ps.gunframe == 9))
				effect = EF_HYPERBLASTER;
			else
				effect = 0;
			if (deathmatch->value)
				damage = 15;
			else
				damage = 20;
			Blaster_Fire (ent, offset, damage, true, effect);
			if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
				P_RemoveAmmo (ent, 1);

			ent->client->anim_priority = ANIM_ATTACK;
			if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crattak1 - 1;
				ent->client->anim_end = FRAME_crattak9;
			}
			else
			{
				ent->s.frame = FRAME_attack1 - 1;
				ent->client->anim_end = FRAME_attack8;
			}
		}

		ent->client->ps.gunframe++;
		if (ent->client->ps.gunframe == 12 && ent->client->pers.inventory[ent->client->ammo_index])
			ent->client->ps.gunframe = 6;
	}

	if (ent->client->ps.gunframe == 12)
	{
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/hyprbd1a.wav"), 1, ATTN_NORM, 0);
		ent->client->weapon_sound = 0;
	}

}

void Weapon_HyperBlaster (edict_t *ent)
{
	static int	pause_frames[]	= {0};
	static int	fire_frames[]	= {6, 7, 8, 9, 10, 11, 0};

	Weapon_Generic (ent, 5, 20, 49, 53, pause_frames, fire_frames, Weapon_HyperBlaster_Fire);
}

/*
======================================================================

MACHINEGUN / CHAINGUN

======================================================================
*/

void Machinegun_Fire (edict_t *ent)
{
	int	i;
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		angles;
	int			damage = 8;
	int			kick = 2;
	vec3_t		offset;

	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->machinegun_shots = 0;
		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->ps.gunframe == 5)
		ent->client->ps.gunframe = 4;
	else
		ent->client->ps.gunframe = 5;

	if (ent->client->pers.inventory[ent->client->ammo_index] < 1)
	{
		ent->client->ps.gunframe = 6;
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange (ent);
		return;
	}

	damage *= damage_multiplier;
	kick *= damage_multiplier;

	for (i=1 ; i<3 ; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
	}
	ent->client->kick_origin[0] = crandom() * 0.35;
	ent->client->kick_angles[0] = ent->client->machinegun_shots * -1.5;

	// the re-release's does not climb as it fires, shakes the same way
	// on every axis, fires where the view is and from its middle
	if (RR_Map ())
	{
		ent->client->kick_angles[0] = crandom() * 0.7;
		AngleVectors (ent->client->v_angle, forward, right, NULL);
		VectorSet(offset, 0, 0, ent->viewheight-8);
	}
	else
	{
		// raise the gun as it is firing
		if (!deathmatch->value)
		{
			ent->client->machinegun_shots++;
			if (ent->client->machinegun_shots > 9)
				ent->client->machinegun_shots = 9;
		}

		// get start / end positions
		VectorAdd (ent->client->v_angle, ent->client->kick_angles, angles);
		AngleVectors (angles, forward, right, NULL);
		VectorSet(offset, 0, 8, ent->viewheight-8);
	}
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_bullet (ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_MACHINEGUN);

	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_MACHINEGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 1);

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (int) (random()+0.25);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (int) (random()+0.25);
		ent->client->anim_end = FRAME_attack8;
	}
}

void Weapon_Machinegun (edict_t *ent)
{
	static int	pause_frames[]	= {23, 45, 0};
	static int	fire_frames[]	= {4, 5, 0};

	Weapon_Generic (ent, 3, 5, 45, 49, pause_frames, fire_frames, Machinegun_Fire);
}

void Chaingun_Fire (edict_t *ent)
{
	int			i;
	int			shots;
	vec3_t		start;
	vec3_t		forward, right, up;
	float		r, u;
	vec3_t		offset;
	int			damage;
	int			kick = 2;

	if (deathmatch->value)
		damage = 6;
	else
		damage = 8;

	if (ent->client->ps.gunframe == 5)
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_IDLE, 0);

	if ((ent->client->ps.gunframe == 14) && !(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe = 32;
		ent->client->weapon_sound = 0;
		return;
	}
	else if ((ent->client->ps.gunframe == 21) && (ent->client->buttons & BUTTON_ATTACK)
		&& ent->client->pers.inventory[ent->client->ammo_index])
	{
		ent->client->ps.gunframe = 15;
	}
	else
	{
		ent->client->ps.gunframe++;
	}

	if (ent->client->ps.gunframe == 22)
	{
		ent->client->weapon_sound = 0;
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
	}
	else
	{
		ent->client->weapon_sound = gi.soundindex("weapons/chngnl1a.wav");
	}

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_attack8;
	}

	if (ent->client->ps.gunframe <= 9)
		shots = 1;
	else if (ent->client->ps.gunframe <= 14)
	{
		if (ent->client->buttons & BUTTON_ATTACK)
			shots = 2;
		else
			shots = 1;
	}
	else
		shots = 3;

	if (ent->client->pers.inventory[ent->client->ammo_index] < shots)
		shots = ent->client->pers.inventory[ent->client->ammo_index];

	if (!shots)
	{
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange (ent);
		return;
	}

	damage *= damage_multiplier;
	kick *= damage_multiplier;

	for (i=0 ; i<3 ; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
		// the re-release's shakes the more the more it fires at once
		if (RR_Map ())
			ent->client->kick_angles[i] = crandom() * (0.5 + shots * 0.15);
	}

	for (i=0 ; i<shots ; i++)
	{
		// get start / end positions; the re-release's about its middle
		AngleVectors (ent->client->v_angle, forward, right, up);
		r = (RR_Map () ? 0 : 7) + crandom()*4;
		u = crandom()*4;
		VectorSet(offset, 0, r, u + ent->viewheight-8);
		P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

		fire_bullet (ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_CHAINGUN);
	}

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte ((MZ_CHAINGUN1 + shots - 1) | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, shots);
}


void Weapon_Chaingun (edict_t *ent)
{
	static int	pause_frames[]	= {38, 43, 51, 61, 0};
	static int	fire_frames[]	= {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 0};

	Weapon_Generic (ent, 4, 31, 61, 64, pause_frames, fire_frames, Chaingun_Fire);
}


/*
======================================================================

SHOTGUN / SUPERSHOTGUN

======================================================================
*/

void weapon_shotgun_fire (edict_t *ent)
{
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		offset;
	int			damage = 4;
	int			kick = 8;

	if (ent->client->ps.gunframe == 9)
	{
		ent->client->ps.gunframe++;
		return;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	// the re-release's from the middle
	VectorSet(offset, 0, RR_Map () ? 0 : 8,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	damage *= damage_multiplier;
	kick *= damage_multiplier;

	if (deathmatch->value)
		fire_shotgun (ent, start, forward, damage, kick, 500, 500, DEFAULT_DEATHMATCH_SHOTGUN_COUNT, MOD_SHOTGUN);
	else
		fire_shotgun (ent, start, forward, damage, kick, 500, 500, DEFAULT_SHOTGUN_COUNT, MOD_SHOTGUN);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_SHOTGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 1);
}

void Weapon_Shotgun (edict_t *ent)
{
	static int	pause_frames[]	= {22, 28, 34, 0};
	static int	fire_frames[]	= {8, 9, 0};

	Weapon_Generic (ent, 7, 18, 36, 39, pause_frames, fire_frames, weapon_shotgun_fire);
}


void weapon_supershotgun_fire (edict_t *ent)
{
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		offset;
	vec3_t		v;
	int			damage = 6;
	int			kick = 12;

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	// the re-release's from the middle
	VectorSet(offset, 0, RR_Map () ? 0 : 8,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	damage *= damage_multiplier;
	kick *= damage_multiplier;

	v[PITCH] = ent->client->v_angle[PITCH];
	v[YAW]   = ent->client->v_angle[YAW] - 5;
	v[ROLL]  = ent->client->v_angle[ROLL];
	AngleVectors (v, forward, right, NULL);
	if (RR_Map ())	// each barrel at what it is pointed at
		P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_shotgun (ent, start, forward, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT/2, MOD_SSHOTGUN);
	v[YAW]   = ent->client->v_angle[YAW] + 5;
	AngleVectors (v, forward, right, NULL);
	if (RR_Map ())
		P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_shotgun (ent, start, forward, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT/2, MOD_SSHOTGUN);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_SSHOTGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 2);
}

void Weapon_SuperShotgun (edict_t *ent)
{
	static int	pause_frames[]	= {29, 42, 57, 0};
	static int	fire_frames[]	= {7, 0};

	Weapon_Generic (ent, 6, 17, 57, 61, pause_frames, fire_frames, weapon_supershotgun_fire);
}



/*
======================================================================

RAILGUN

======================================================================
*/

void weapon_railgun_fire (edict_t *ent)
{
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		offset;
	int			damage;
	int			kick;

	if (deathmatch->value)
	{	// normal damage is too extreme in dm
		damage = 100;
		kick = 200;
	}
	else
	{
		damage = 150;
		kick = 250;
	}

	damage *= damage_multiplier;
	kick *= damage_multiplier;

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -3, ent->client->kick_origin);
	ent->client->kick_angles[0] = -3;

	VectorSet(offset, 0, 7,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_rail (ent, start, forward, damage, kick);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_RAILGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 1);
}


void Weapon_Railgun (edict_t *ent)
{
	static int	pause_frames[]	= {56, 0};
	static int	fire_frames[]	= {4, 0};

	Weapon_Generic (ent, 3, 18, 56, 61, pause_frames, fire_frames, weapon_railgun_fire);
}


/*
======================================================================

BFG10K

======================================================================
*/

void weapon_bfg_fire (edict_t *ent)
{
	vec3_t	offset, start;
	vec3_t	forward, right;
	int		damage;
	float	damage_radius = 1000;

	if (deathmatch->value)
		damage = 200;
	else
		damage = 500;

	if (ent->client->ps.gunframe == 9)
	{
		// send muzzle flash
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (ent-g_edicts);
		gi.WriteByte (MZ_BFG | is_silenced);
		gi.multicast (ent->s.origin, MULTICAST_PVS);

		ent->client->ps.gunframe++;

		PlayerNoise(ent, ent->s.origin, PNOISE_WEAPON);
		return;
	}

	// cells can go down during windup (from power armor hits), so
	// check again and abort firing if we don't have enough now
	if (ent->client->pers.inventory[ent->client->ammo_index] < 50)
	{
		ent->client->ps.gunframe++;
		return;
	}

	damage *= damage_multiplier;

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);

	// make a big pitch kick with an inverse fall; the re-release's half
	// as big, as a weapon's kick
	if (RR_Map ())
	{
		ent->client->kick_angles[0] = -20;
		ent->client->kick_angles[2] = crandom()*8;
		ent->client->kick_length = DAMAGE_TIME;
	}
	else
	{
		ent->client->v_dmg_pitch = -40;
		ent->client->v_dmg_roll = crandom()*8;
		ent->client->v_dmg_time = level.time + DAMAGE_TIME;
	}

	VectorSet(offset, 8, 8, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_bfg (ent, start, forward, damage, 400, damage_radius);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 50);
}

void Weapon_BFG (edict_t *ent)
{
	static int	pause_frames[]	= {39, 45, 50, 55, 0};
	static int	fire_frames[]	= {9, 17, 0};

	Weapon_Generic (ent, 8, 32, 55, 58, pause_frames, fire_frames, weapon_bfg_fire);
}


//======================================================================

/*
======================================================================

The mission packs' weapons

======================================================================
*/

/*
The Reckoning's phalanx: two mag slug plasma shots, left and right
*/
void weapon_phalanx_fire (edict_t *ent)
{
	vec3_t	start, forward, right, up, offset, v;
	int		damage, radius_damage;
	float	damage_radius;

	damage = 70 + (int)(random() * 10.0);
	damage *= damage_multiplier;
	// (its radius damage is 30 whatever: the original's)
	radius_damage = 30;
	damage_radius = 120;

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	VectorSet(offset, 0, 8, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	v[PITCH] = ent->client->v_angle[PITCH];
	v[ROLL] = ent->client->v_angle[ROLL];

	if (ent->client->ps.gunframe == 8)
	{
		v[YAW] = ent->client->v_angle[YAW] - 1.5;
		AngleVectors (v, forward, right, up);

		fire_plasma (ent, start, forward, damage, 725, damage_radius, radius_damage);

		if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
			P_RemoveAmmo (ent, 1);
	}
	else
	{
		v[YAW] = ent->client->v_angle[YAW] + 1.5;
		AngleVectors (v, forward, right, up);

		fire_plasma (ent, start, forward, damage, 725, damage_radius, radius_damage);

		// send muzzle flash
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (ent-g_edicts);
		gi.WriteByte (MZ_PHALANX | is_silenced);
		gi.multicast (ent->s.origin, MULTICAST_PVS);

		PlayerNoise(ent, start, PNOISE_WEAPON);
	}

	ent->client->ps.gunframe++;
}

void Weapon_Phalanx (edict_t *ent)
{
	static int	pause_frames[]	= {29, 42, 55, 0};
	static int	fire_frames[]	= {7, 8, 0};

	Weapon_Generic (ent, 5, 20, 58, 63, pause_frames, fire_frames, weapon_phalanx_fire);
}

/*
Ground Zero's chainfist
*/
#define	CHAINFIST_REACH		64

void fire_player_melee (edict_t *self, vec3_t start, vec3_t aim, int reach, int damage, int kick, int quiet, int mod);

void weapon_chainfist_fire (edict_t *ent)
{
	vec3_t	offset, forward, right, up, start;
	int		damage;

	damage = deathmatch->value ? 30 : 15;
	damage *= damage_multiplier;

	AngleVectors (ent->client->v_angle, forward, right, up);

	// kick back
	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	VectorSet (offset, 0, 8, ent->viewheight-4);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	fire_player_melee (ent, start, forward, CHAINFIST_REACH, damage, 100, 1, MOD_CHAINFIST);

	PlayerNoise (ent, start, PNOISE_WEAPON);

	ent->client->ps.gunframe++;
	P_RemoveAmmo (ent, ent->client->pers.weapon->quantity);
}

// smoke from its motor: it's a two-stroke, you know
static void chainfist_smoke (edict_t *ent)
{
	vec3_t	tempVec, forward, right, up, offset;

	AngleVectors (ent->client->v_angle, forward, right, up);
	VectorSet (offset, 8, 8, ent->viewheight-4);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, tempVec);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_CHAINFIST_SMOKE);
	gi.WritePosition (tempVec);
	gi.unicast (ent, 0);
}

void Weapon_ChainFist (edict_t *ent)
{
	static int	pause_frames[]	= {0};
	static int	fire_frames[]	= {8, 9, 16, 17, 18, 30, 31, 0};
	float	chance;
	int		last_sequence = 0;

	if (ent->client->ps.gunframe == 13 || ent->client->ps.gunframe == 23)
		ent->client->ps.gunframe = 32;		// end of an attack: go idle
	// holds for the idle sequence
	else if (ent->client->ps.gunframe == 42 && (rand() & 7))
	{
		if (ent->client->pers.hand != CENTER_HANDED && random() < 0.4)
			chainfist_smoke (ent);
	}
	else if (ent->client->ps.gunframe == 51 && (rand() & 7))
	{
		if (ent->client->pers.hand != CENTER_HANDED && random() < 0.4)
			chainfist_smoke (ent);
	}

	// its sound
	if (ent->client->weaponstate == WEAPON_FIRING)
		ent->client->weapon_sound = gi.soundindex ("weapons/sawhit.wav");
	else if (ent->client->weaponstate == WEAPON_DROPPING)
		ent->client->weapon_sound = 0;
	else
		ent->client->weapon_sound = gi.soundindex ("weapons/sawidle.wav");

	Weapon_Generic (ent, 4, 32, 57, 60, pause_frames, fire_frames, weapon_chainfist_fire);

	if (ent->client->buttons & BUTTON_ATTACK)
	{
		if (ent->client->ps.gunframe == 13 || ent->client->ps.gunframe == 23 || ent->client->ps.gunframe == 32)
		{
			last_sequence = ent->client->ps.gunframe;
			ent->client->ps.gunframe = 6;
		}
	}

	if (ent->client->ps.gunframe == 6)
	{
		// one of its three sequences, not the last
		chance = random();
		if (last_sequence == 13)
			chance -= 0.34;
		else if (last_sequence == 23)
			chance += 0.33;
		else if (last_sequence == 32 && chance >= 0.33)
			chance += 0.34;

		if (chance < 0.33)
			ent->client->ps.gunframe = 14;
		else if (chance < 0.66)
			ent->client->ps.gunframe = 24;
	}
}

/*
Ground Zero's disruptor: a tracker, homing on what it's aimed at
*/
void weapon_tracker_fire (edict_t *self)
{
	vec3_t	forward, right, start, end, offset, mins, maxs;
	edict_t	*enemy;
	trace_t	tr;
	int		damage;

	damage = deathmatch->value ? 30 : 45;
	damage *= damage_multiplier;

	VectorSet (mins, -16, -16, -16);
	VectorSet (maxs, 16, 16, 16);
	AngleVectors (self->client->v_angle, forward, right, NULL);
	VectorSet (offset, 24, 8, self->viewheight-8);
	P_ProjectSource (self->client, self->s.origin, offset, forward, right, start);

	// what it's aimed at, or near
	VectorMA (start, 8192, forward, end);
	enemy = NULL;
	tr = gi.trace (start, vec3_origin, vec3_origin, end, self, MASK_SHOT);
	if (tr.ent == world)
		tr = gi.trace (start, mins, maxs, end, self, MASK_SHOT);
	if (tr.ent != world && ((tr.ent->svflags & SVF_MONSTER) || tr.ent->client) && tr.ent->health > 0)
		enemy = tr.ent;

	VectorScale (forward, -2, self->client->kick_origin);
	self->client->kick_angles[0] = -1;

	fire_tracker (self, start, forward, damage, 1000, enemy);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (self-g_edicts);
	gi.WriteByte (MZ_TRACKER);
	gi.multicast (self->s.origin, MULTICAST_PVS);

	PlayerNoise (self, start, PNOISE_WEAPON);

	self->client->ps.gunframe++;
	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (self, self->client->pers.weapon->quantity);
}

void Weapon_Disintegrator (edict_t *ent)
{
	static int	pause_frames[]	= {14, 19, 23, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 9, 29, 34, pause_frames, fire_frames, weapon_tracker_fire);
}

/*
Ground Zero's heat beam (the re-release's plasma beam), with the
re-release's one view model
*/
#define	HEATBEAM_DM_DMG		15
#define	HEATBEAM_SP_DMG		15

void fire_heatbeam (edict_t *self, vec3_t start, vec3_t aimdir, vec3_t offset, int damage, int kick, qboolean monster);

void Heatbeam_Fire (edict_t *ent)
{
	vec3_t	start, forward, right, up, offset;
	int		damage, kick;

	if (deathmatch->value)
	{
		damage = HEATBEAM_DM_DMG;
		kick = 75;		// really knock 'em around in deathmatch
	}
	else
	{
		damage = HEATBEAM_SP_DMG;
		kick = 30;
	}

	ent->client->ps.gunframe++;

	damage *= damage_multiplier;
	kick *= damage_multiplier;

	VectorClear (ent->client->kick_origin);
	VectorClear (ent->client->kick_angles);

	AngleVectors (ent->client->v_angle, forward, right, up);

	// the "view" offset, the beam's start, for the trace
	VectorSet (offset, 7, 2, ent->viewheight-3);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	// and the entity's
	VectorSet (offset, 2, 7, -3);

	fire_heatbeam (ent, start, forward, offset, damage, kick, false);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_HEATBEAM | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise (ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, ent->client->pers.weapon->quantity);

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - 1;
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - 1;
		ent->client->anim_end = FRAME_attack8;
	}
}

void Weapon_Heatbeam (edict_t *ent)
{
	static int	pause_frames[]	= {35, 0};
	static int	fire_frames[]	= {9, 10, 11, 12, 0};

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		ent->client->weapon_sound = gi.soundindex ("weapons/bfg__l1a.wav");

		// it fires on, while it has the cells and the button's held
		if (ent->client->pers.inventory[ent->client->ammo_index] >= 2 && (ent->client->buttons & BUTTON_ATTACK))
		{
			if (ent->client->ps.gunframe >= 13)
				ent->client->ps.gunframe = 9;
		}
		else
			ent->client->ps.gunframe = 13;
	}
	else
		ent->client->weapon_sound = 0;

	Weapon_Generic (ent, 8, 12, 39, 44, pause_frames, fire_frames, Heatbeam_Fire);
}

/*
Ground Zero's tesla mine: thrown, as the re-release has it, from its one
view model
*/
void fire_tesla (edict_t *self, vec3_t start, vec3_t aimdir, int damage_multiplier, int speed);

void weapon_tesla_fire (edict_t *ent, qboolean held)
{
	vec3_t	offset, forward, right, up, start, d;
	float	timer;
	int		speed;

	if (damage_multiplier >= 4)
		gi.sound (ent, CHAN_ITEM, gi.soundindex ("items/damage3.wav"), 1, ATTN_NORM, 0);
	else if (damage_multiplier == 2)
		gi.sound (ent, CHAN_ITEM, gi.soundindex ("misc/ddamage3.wav"), 1, ATTN_NORM, 0);

	AngleVectors (ent->client->v_angle, forward, right, up);

	// from its offset along up too
	VectorSet (offset, 0, -4, ent->viewheight-22);
	if (ent->client->pers.hand == LEFT_HANDED)
		offset[1] *= -1;
	else if (ent->client->pers.hand == CENTER_HANDED)
		offset[1] = 0;
	VectorCopy (ent->s.origin, d);
	VectorMA (d, offset[0], forward, d);
	VectorMA (d, offset[1], right, d);
	VectorMA (d, offset[2], up, start);

	timer = ent->client->grenade_time - level.time;
	speed = GRENADE_MINSPEED + (GRENADE_TIMER - timer) * ((GRENADE_MAXSPEED - GRENADE_MINSPEED) / GRENADE_TIMER);
	if (speed > GRENADE_MAXSPEED)
		speed = GRENADE_MAXSPEED;

	fire_tesla (ent, start, forward, damage_multiplier, speed);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 1);

	ent->client->grenade_time = level.time + 1.0;

	if (ent->deadflag || ent->s.modelindex != 255)		// VWep animations screw up corpses
		return;
	if (ent->health <= 0)
		return;

	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->client->anim_priority = ANIM_ATTACK;
		ent->s.frame = FRAME_crattak1-1;
		ent->client->anim_end = FRAME_crattak3;
	}
	else
	{
		ent->client->anim_priority = ANIM_REVERSE;
		ent->s.frame = FRAME_wave08;
		ent->client->anim_end = FRAME_wave01;
	}
}

// Ground Zero's generic throw: the hand grenade's, by frames
static void Throw_Generic (edict_t *ent, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_THROW_SOUND, char *prime_sound,
	int FRAME_THROW_HOLD, int FRAME_THROW_FIRE, int *pause_frames, int EXPLODE, char *primed_sound, void (*fire)(edict_t *ent, qboolean held))
{
	int		n;

	if (ent->client->newweapon && ent->client->weaponstate == WEAPON_READY)
	{
		ChangeWeapon (ent);
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		ent->client->weaponstate = WEAPON_READY;
		ent->client->ps.gunframe = FRAME_IDLE_FIRST;
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK)
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			if (ent->client->pers.inventory[ent->client->ammo_index])
			{
				ent->client->ps.gunframe = 1;
				ent->client->weaponstate = WEAPON_FIRING;
				ent->client->grenade_time = 0;
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}
				NoAmmoWeaponChange (ent);
			}
			return;
		}

		if (ent->client->ps.gunframe == FRAME_IDLE_LAST)
		{
			ent->client->ps.gunframe = FRAME_IDLE_FIRST;
			return;
		}

		for (n = 0; pause_frames[n]; n++)
			if (ent->client->ps.gunframe == pause_frames[n] && (rand() & 15))
				return;

		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		if (prime_sound && ent->client->ps.gunframe == FRAME_THROW_SOUND)
			gi.sound (ent, CHAN_WEAPON, gi.soundindex (prime_sound), 1, ATTN_NORM, 0);

		if (ent->client->ps.gunframe == FRAME_THROW_HOLD)
		{
			if (!ent->client->grenade_time)
				ent->client->grenade_time = level.time + GRENADE_TIMER + 0.2;
			// its sound while held
			if (primed_sound && !ent->client->grenade_blew_up)
				ent->client->weapon_sound = gi.soundindex (primed_sound);

			// held too long: in the hand
			if (EXPLODE && !ent->client->grenade_blew_up && level.time >= ent->client->grenade_time)
			{
				ent->client->weapon_sound = 0;
				fire (ent, true);
				ent->client->grenade_blew_up = true;
			}

			if (ent->client->buttons & BUTTON_ATTACK)
				return;

			if (ent->client->grenade_blew_up)
			{
				if (level.time >= ent->client->grenade_time)
				{
					ent->client->ps.gunframe = FRAME_FIRE_LAST;
					ent->client->grenade_blew_up = false;
				}
				else
					return;
			}
		}

		if (ent->client->ps.gunframe == FRAME_THROW_FIRE)
		{
			ent->client->weapon_sound = 0;
			fire (ent, true);
		}

		if (ent->client->ps.gunframe == FRAME_FIRE_LAST && level.time < ent->client->grenade_time)
			return;

		ent->client->ps.gunframe++;

		if (ent->client->ps.gunframe == FRAME_IDLE_FIRST)
		{
			ent->client->grenade_time = 0;
			ent->client->weaponstate = WEAPON_READY;
		}
	}
}

void Weapon_Tesla (edict_t *ent)
{
	static int	pause_frames[]	= {21, 0};

	// (no prime sound, as the re-release has it)
	Throw_Generic (ent, 8, 32, -1, NULL, 1, 2, pause_frames, 0, NULL, weapon_tesla_fire);
}

/*
======================================================================

Ground Zero's ETF rifle and prox launcher, as the re-release has them

======================================================================
*/

/*
The ETF rifle: flechettes from each barrel in turn, while the button is
held
*/
void weapon_etf_rifle_fire (edict_t *ent)
{
	vec3_t	forward, right, start, offset, angles;
	int		damage = 10;
	int		kick = 3;
	int		i;

	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe = 8;
		return;
	}

	if (ent->client->ps.gunframe == 6)
		ent->client->ps.gunframe = 7;
	else
		ent->client->ps.gunframe = 6;

	if (ent->client->pers.inventory[ent->client->ammo_index] < ent->client->pers.weapon->quantity)
	{
		ent->client->ps.gunframe = 8;
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange (ent);
		return;
	}

	damage *= damage_multiplier;
	kick *= damage_multiplier;

	for (i = 0; i < 3; i++)
	{
		ent->client->kick_origin[i] = crandom () * 0.85;
		ent->client->kick_angles[i] = crandom () * 0.85;
	}

	// get start / end positions
	VectorAdd (ent->client->v_angle, ent->client->kick_angles, angles);
	AngleVectors (angles, forward, right, NULL);
	if (ent->client->ps.gunframe == 6)
		VectorSet (offset, 15, 8, ent->viewheight-8);
	else
		VectorSet (offset, 15, 6, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_flechette (ent, start, forward, damage, 1150, kick);

	// send muzzle flash, one for each barrel
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte ((ent->client->ps.gunframe == 6 ? MZ_ETF_RIFLE : MZ_ETF_RIFLE_2) | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise (ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, ent->client->pers.weapon->quantity);

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (int) (random () + 0.25);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (int) (random () + 0.25);
		ent->client->anim_end = FRAME_attack8;
	}
}

void Weapon_ETF_Rifle (edict_t *ent)
{
	static int	pause_frames[]	= {18, 28, 0};
	static int	fire_frames[]	= {6, 7, 0};

	Weapon_Generic (ent, 4, 7, 37, 41, pause_frames, fire_frames, weapon_etf_rifle_fire);
}

/*
The prox launcher: a mine lobbed as a grenade is, that sticks where it
lands and goes off when something comes near
*/
void weapon_prox_fire (edict_t *ent)
{
	vec3_t	offset, forward, right, start, angles;

	// not so far up as to fire behind
	VectorCopy (ent->client->v_angle, angles);
	if (angles[PITCH] < -62.5)
		angles[PITCH] = -62.5;
	AngleVectors (angles, forward, right, NULL);

	VectorSet (offset, 8, 0, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	fire_prox (ent, start, forward, damage_multiplier, 600);

	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_PROX | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise (ent, start, PNOISE_WEAPON);

	ent->client->ps.gunframe++;

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 1);
}

void Weapon_ProxLauncher (edict_t *ent)
{
	static int	pause_frames[]	= {34, 51, 59, 0};
	static int	fire_frames[]	= {6, 0};

	Weapon_Generic (ent, 5, 16, 59, 64, pause_frames, fire_frames, weapon_prox_fire);
}

/*
======================================================================

The Reckoning's weapons, as the re-release has them

======================================================================
*/

/*
The ion ripper: a boomerang of cells, a little off the line of sight
*/
void weapon_ionripper_fire (edict_t *ent)
{
	vec3_t	start, forward, right, offset, tempang;
	int		damage;

	if (deathmatch->value)
		// tone down for deathmatch
		damage = 30;
	else
		damage = 50;
	damage *= damage_multiplier;

	VectorCopy (ent->client->v_angle, tempang);
	tempang[YAW] += crandom ();
	AngleVectors (tempang, forward, right, NULL);

	VectorScale (forward, -3, ent->client->kick_origin);
	ent->client->kick_angles[0] = -3;

	VectorSet (offset, 16, 7, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	fire_ionripper (ent, start, forward, damage, 500, EF_IONRIPPER);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_IONRIPPER | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise (ent, start, PNOISE_WEAPON);

	ent->client->ps.gunframe++;

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, ent->client->pers.weapon->quantity);
}

void Weapon_Ionripper (edict_t *ent)
{
	static int	pause_frames[]	= {36, 0};
	static int	fire_frames[]	= {6, 0};

	Weapon_Generic (ent, 5, 7, 36, 39, pause_frames, fire_frames, weapon_ionripper_fire);
}

/*
The trap: thrown as a hand grenade is, the longer held the further
*/
#define TRAP_TIMER		5.0
#define TRAP_MINSPEED	300
#define TRAP_MAXSPEED	700

void weapon_trap_fire (edict_t *ent, qboolean held)
{
	vec3_t	offset, forward, right, start, angles;
	float	timer;
	int		speed;

	// not so far up as to go behind
	VectorCopy (ent->client->v_angle, angles);
	if (angles[PITCH] < -62.5)
		angles[PITCH] = -62.5;
	AngleVectors (angles, forward, right, NULL);

	VectorSet (offset, 8, 0, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	timer = ent->client->grenade_time - level.time;
	if (ent->health <= 0)
		speed = TRAP_MINSPEED;
	else
	{
		speed = TRAP_MINSPEED + (TRAP_TIMER - timer) * ((TRAP_MAXSPEED - TRAP_MINSPEED) / TRAP_TIMER);
		if (speed > TRAP_MAXSPEED)
			speed = TRAP_MAXSPEED;
	}

	ent->client->grenade_time = 0;

	fire_trap (ent, start, forward, speed);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		P_RemoveAmmo (ent, 1);
}

void Weapon_Trap (edict_t *ent)
{
	static int	pause_frames[]	= {29, 34, 39, 48, 0};

	Throw_Generic (ent, 15, 48, 5, "weapons/trapcock.wav", 11, 12, pause_frames, 0, "weapons/traploop.wav", weapon_trap_fire);
}
