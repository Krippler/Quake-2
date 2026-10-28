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
cl_wheel.c -- the 2023 re-release's weapon and powerup wheels

Held (+wheel, +wheel2; its default.cfg binds them to e and q), a wheel shows
round the middle of the screen the weapons, or the things to use, that you
have; the mouse, or the look stick, moves round it rather than the view,
and let go, what it is on is used. The game names what goes on them, in its
order (CS_WHEEL_WEAPONS, CS_WHEEL_POWERUPS); what of them you have it sends
when asked ("wheelinv", the inventory, again every half second while one is
held). Their pictures are the re-release's (pics/wheel/), the inventory's
without them.

cl_weapnext and cl_weapprev, its mouse wheel's, are weapnext and weapprev.
*/

#include "client.h"

#define WHEEL_RADIUS	100		// how far the cursor goes
#define WHEEL_DEADZONE	0.35	// of it, the cursor nearer the middle chooses none
#define WHEEL_SIZE		0.33	// the icons' circle's radius, of the screen's height

typedef struct
{
	int		item;
	int		ammo;		// item, or -1
	int		minammo;
	char	icon[MAX_QPATH];
} wheelslot_t;

static int		wheel_open;		// 0, or 1 the weapons, 2 the powerups
static float	wheel_x, wheel_y;	// the cursor, from the middle
static int		wheel_asked;	// cls.realtime of the last wheelinv
static int		wheel_selected = -1;	// the item it is on

static cvar_t	*cl_wheel_sensitivity;

/*
=================
CL_WheelSlots

What of a wheel's things you have
=================
*/
static int CL_WheelSlots (int which, wheelslot_t *slots)
{
	int			i, n, cs;
	char		*s;
	wheelslot_t	*sl;

	cs = which == 1 ? CS_WHEEL_WEAPONS : CS_WHEEL_POWERUPS;
	for (i=n=0 ; i<MAX_WHEEL_ITEMS ; i++)
	{
		s = cl.configstrings[cs + i];
		if (!s[0])
			break;
		sl = &slots[n];
		sl->ammo = -1;
		sl->minammo = 0;
		if (which == 1)
		{
			if (sscanf (s, "%i %i %i %63s", &sl->item, &sl->ammo, &sl->minammo, sl->icon) != 4)
				continue;
		}
		else if (sscanf (s, "%i %63s", &sl->item, sl->icon) != 2)
			continue;
		if (sl->item <= 0 || sl->item >= MAX_ITEMS || sl->ammo >= MAX_ITEMS)
			continue;
		if (cl.inventory[sl->item] > 0)
			n++;
	}
	return n;
}

static qboolean CL_WheelUsable (wheelslot_t *sl)
{
	return sl->ammo < 0 || cl.inventory[sl->ammo] >= sl->minammo;
}

// the game asked, if it is one that knows wheelinv (the 1997 mission
// packs' would say it, as chat)
static void CL_WheelAsk (void)
{
	if (cls.state != ca_active || !cl.configstrings[CS_WHEEL_WEAPONS][0])
		return;
	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message, "wheelinv");
	wheel_asked = cls.realtime;
}

static void CL_WheelOpen (int which)
{
	if (cls.state != ca_active || cls.key_dest != key_game)
		return;
	if (!cl.configstrings[which == 1 ? CS_WHEEL_WEAPONS : CS_WHEEL_POWERUPS][0])
		return;
	wheel_open = which;
	wheel_x = wheel_y = 0;
	wheel_selected = -1;
	CL_WheelAsk ();
}

static void CL_WheelClose (int which)
{
	if (wheel_open != which)
		return;
	wheel_open = 0;
	if (wheel_selected > 0 && cls.state == ca_active)
		Cbuf_AddText (va("use %s\n", cl.configstrings[CS_ITEMS + wheel_selected]));
	wheel_selected = -1;
}

static void CL_Wheel1Down (void) { CL_WheelOpen (1); }
static void CL_Wheel1Up (void) { CL_WheelClose (1); }
static void CL_Wheel2Down (void) { CL_WheelOpen (2); }
static void CL_Wheel2Up (void) { CL_WheelClose (2); }

static void CL_WeapNext_f (void) { Cbuf_AddText ("weapnext\n"); }
static void CL_WeapPrev_f (void) { Cbuf_AddText ("weapprev\n"); }

/*
=================
CL_WheelActive

A wheel held: the view stays, and the trigger is not pulled (CL_CreateCmd)
=================
*/
qboolean CL_WheelActive (void)
{
	if (wheel_open && (cls.state != ca_active || cls.key_dest != key_game))
	{
		wheel_open = 0;		// a menu or the console came up: nothing chosen
		wheel_selected = -1;
	}
	return wheel_open != 0;
}

/*
=================
CL_WheelMove

How far the view would have turned, degrees, moves the cursor
=================
*/
void CL_WheelMove (float yaw, float pitch)
{
	float	len, max;

	wheel_x -= yaw * cl_wheel_sensitivity->value;
	wheel_y += pitch * cl_wheel_sensitivity->value;
	max = WHEEL_RADIUS;
	len = sqrt (wheel_x*wheel_x + wheel_y*wheel_y);
	if (len > max)
	{
		wheel_x *= max / len;
		wheel_y *= max / len;
	}
}

/*
=================
CL_DrawWheel
=================
*/
void CL_DrawWheel (void)
{
	wheelslot_t	slots[MAX_WHEEL_ITEMS], *sl;
	int			i, n, x, y, w, h, sw, sh, cx, cy, chosen, count;
	float		a, best, d, ca, r;
	char		pic[MAX_QPATH], *s;

	if (!CL_WheelActive ())
		return;

	if (cls.realtime - wheel_asked > 500)
		CL_WheelAsk ();

	n = CL_WheelSlots (wheel_open, slots);

	re.DrawFadeScreen ();		// the view dimmed behind it

	r = viddef.height * WHEEL_SIZE;
	cx = viddef.width / 2;
	cy = viddef.height / 2;

	// the slot nearest the cursor's direction, if it is out of the middle
	chosen = -1;
	if (n && sqrt (wheel_x*wheel_x + wheel_y*wheel_y) > WHEEL_RADIUS * WHEEL_DEADZONE)
	{
		ca = atan2 (wheel_y, wheel_x);
		best = 10;
		for (i=0 ; i<n ; i++)
		{
			a = -M_PI/2 + 2*M_PI*i/n;		// the first at the top
			d = fabs (atan2 (sin (ca - a), cos (ca - a)));
			if (d < best)
			{
				best = d;
				chosen = i;
			}
		}
	}
	wheel_selected = chosen >= 0 && CL_WheelUsable (&slots[chosen]) ? slots[chosen].item : -1;

	for (i=0 ; i<n ; i++)
	{
		sl = &slots[i];
		a = -M_PI/2 + 2*M_PI*i/n;
		x = cx + cos (a) * r;
		y = cy + sin (a) * r;

		// the re-release's picture, lit when chosen; the inventory's
		Com_sprintf (pic, sizeof(pic), "/pics/wheel/%s%s.png", sl->icon,
			i == chosen && CL_WheelUsable (sl) ? "_selected" : "");
		re.DrawGetPicSize (&w, &h, pic);
		if (!w)
		{
			Com_sprintf (pic, sizeof(pic), "/pics/wheel/%s.png", sl->icon);
			re.DrawGetPicSize (&w, &h, pic);
		}
		if (w)
		{
			// 24 pixels square, half as big again on a screen 480 high
			sw = (w * viddef.height / 320 + 3) & ~3;
			sh = h * viddef.height / 320;
			re.DrawStretchPic (x - sw/2, y - sh/2, sw, sh, pic);
			h = sh;
		}
		else
		{
			Com_sprintf (pic, sizeof(pic), "%s", sl->icon);
			re.DrawGetPicSize (&w, &h, pic);
			re.DrawPic (x - w/2, y - h/2, pic);
		}

		// its ammo under it, or that it has too little
		if (sl->ammo >= 0)
		{
			count = cl.inventory[sl->ammo];
			s = va("%i", count);
			if (!CL_WheelUsable (sl))
				s = "--";
			DrawAltString (x - strlen (s)*4, y + h/2 + 2, s);
		}
	}

	// the cursor, and what it is on
	re.DrawFill (cx + wheel_x*r/WHEEL_RADIUS - 2, cy + wheel_y*r/WHEEL_RADIUS - 2, 4, 4, 0xdf);
	if (chosen >= 0)
	{
		s = cl.configstrings[CS_ITEMS + slots[chosen].item];
		DrawString (cx - strlen (s)*4, cy - 4, s);
		if (!CL_WheelUsable (&slots[chosen]))
			DrawAltString (cx - 7*4, cy + 6, "no ammo");
	}
	else if (!n)
	{
		s = wheel_open == 1 ? "no weapons" : "nothing to use";
		DrawString (cx - strlen (s)*4, cy - 4, s);
	}
}

void CL_InitWheel (void)
{
	Cmd_AddCommand ("+wheel", CL_Wheel1Down);
	Cmd_AddCommand ("-wheel", CL_Wheel1Up);
	Cmd_AddCommand ("+wheel2", CL_Wheel2Down);
	Cmd_AddCommand ("-wheel2", CL_Wheel2Up);
	Cmd_AddCommand ("cl_weapnext", CL_WeapNext_f);
	Cmd_AddCommand ("cl_weapprev", CL_WeapPrev_f);
	cl_wheel_sensitivity = Cvar_Get ("cl_wheel_sensitivity", "15", CVAR_ARCHIVE);
}
