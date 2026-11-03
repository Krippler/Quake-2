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
cl_damage.c -- the 2023 re-release's damage indicators and hit markers

Hurt, an arc round the crosshair points to where it came from for a
second: red for health, yellow for armour, green for power armour, longer
the more it was. Hurting something, the re-release's marker
(pics/marker.png) shows round the crosshair and its sound plays
(weapons/marker.wav).

The game says what (svc_damage, this engine's, sent to q2ext clients;
the re-release's svc_damage and STAT_HIT_MARKER).

cl_damage_indicators 0 turns off the arcs; cl_hit_markers 0 the marker,
1 its picture alone, 2 (the default) its sound too.
*/

#include "client.h"

#define MAX_INDICATORS			8
#define INDICATOR_MSEC			1000
#define HIT_MARKER_MSEC			250

typedef struct
{
	vec3_t	dir;		// from the player to where it came from
	int		encoded;	// as the game sent it
	int		time;		// cls.realtime it came
} indicator_t;

static indicator_t	indicators[MAX_INDICATORS];
static int			next_indicator;
static int			hit_marker_time;

static cvar_t		*cl_damage_indicators;
static cvar_t		*cl_hit_markers;

/*
=================
CL_ParseDamage

svc_damage: [short] hit marker damage, [byte] count,
count * ([byte] encoded, [dir] from the player to where it came from)
=================
*/
void CL_ParseDamage (void)
{
	int			i, hit, count, encoded;
	vec3_t		dir;
	indicator_t	*ind;

	hit = MSG_ReadShort (&net_message);
	count = MSG_ReadByte (&net_message);
	for (i = 0 ; i < count ; i++)
	{
		encoded = MSG_ReadByte (&net_message);
		MSG_ReadDir (&net_message, dir);

		ind = &indicators[next_indicator++ % MAX_INDICATORS];
		VectorNegate (dir, ind->dir);
		ind->encoded = encoded;
		ind->time = cls.realtime;
	}

	if (hit > 0 && cl_hit_markers->value)
	{
		hit_marker_time = cls.realtime;
		if (cl_hit_markers->value >= 2 && cl.sfx_hit_marker)
			S_StartLocalSound ("weapons/marker.wav");
	}
}

/*
=================
CL_RegisterDamageSounds

The marker's sound, if the data has it (the re-release's)
=================
*/
void CL_RegisterDamageSounds (void)
{
	cl.sfx_hit_marker = FS_LoadFile ("sound/weapons/marker.wav", NULL) > 0;
	if (cl.sfx_hit_marker)
		S_RegisterSound ("weapons/marker.wav");
	memset (indicators, 0, sizeof(indicators));
	hit_marker_time = 0;
}

/*
=================
CL_DrawIndicator
=================
*/
static void CL_DrawIndicator (indicator_t *ind, int cx, int cy, float radius, int size)
{
	float	yaw, a, x, y, span, step, f;
	int		color, i, n, age;

	age = cls.realtime - ind->time;
	if (age < 0 || age >= INDICATOR_MSEC)
		return;

	// where it came from, turned to the view: ahead is up the screen
	yaw = cl.refdef.viewangles[YAW] * M_PI / 180;
	x = ind->dir[0] * sin (yaw) - ind->dir[1] * cos (yaw);		// to the right
	y = ind->dir[0] * cos (yaw) + ind->dir[1] * sin (yaw);		// ahead
	if (!x && !y)
		return;
	a = atan2 (x, y);

	if (ind->encoded & 0x20)
		color = 0xf2;		// health
	else if (ind->encoded & 0x40)
		color = 0xdc;		// armour
	else
		color = 0xd0;		// power armour

	// longer the more it was: a third of the damage, up to 31
	span = (12 + (ind->encoded & 0x1f) * 1.2) * M_PI / 180;
	step = (float)size / radius;
	n = span / step;
	if (n < 2)
		n = 2;

	for (i = 0 ; i <= n ; i++)
	{
		// thinning out as it goes, in its second half
		f = (float)age / INDICATOR_MSEC;
		if (f > 0.5 && (i & 1))
			continue;
		if (f > 0.8 && (i & 2))
			continue;
		x = a - span / 2 + span * i / n;
		re.DrawFill (cx + sin (x) * radius - size / 2, cy - cos (x) * radius - size / 2,
			size, size, color);
	}
}

/*
=================
CL_DrawDamage

The arcs and the marker, round the middle of the view
=================
*/
void CL_DrawDamage (void)
{
	int		i, cx, cy, size, w, h, age;
	float	radius, scale;

	if (cls.state != ca_active || cl.cinematictime > 0)
		return;

	cx = scr_vrect.x + scr_vrect.width / 2;
	cy = scr_vrect.y + scr_vrect.height / 2;
	scale = viddef.height / 240.0;

	if (cl_damage_indicators->value)
	{
		radius = 40 * scale;
		size = scale * 2 + 0.5;
		if (size < 2)
			size = 2;
		for (i = 0 ; i < MAX_INDICATORS ; i++)
			if (indicators[i].time)
				CL_DrawIndicator (&indicators[i], cx, cy, radius, size);
	}

	age = cls.realtime - hit_marker_time;
	if (hit_marker_time && age >= 0 && age < HIT_MARKER_MSEC && cl_hit_markers->value)
	{
		re.DrawGetPicSize (&w, &h, "/pics/marker.png");
		if (w)
		{
			// 48 pixels square on a screen 480 high
			w = (int)(w * viddef.height / 480 + 3) & ~3;
			h = h * viddef.height / 480;
			re.DrawStretchPic (cx - w / 2, cy - h / 2, w, h, "/pics/marker.png");
		}
		else
		{
			// without the re-release's picture, its four corners
			size = scale + 0.5;
			for (i = 2 ; i < 6 ; i++)
			{
				re.DrawFill (cx - (i + 2) * size, cy - (i + 2) * size, size, size, 0xd7);
				re.DrawFill (cx + (i + 1) * size, cy - (i + 2) * size, size, size, 0xd7);
				re.DrawFill (cx - (i + 2) * size, cy + (i + 1) * size, size, size, 0xd7);
				re.DrawFill (cx + (i + 1) * size, cy + (i + 1) * size, size, size, 0xd7);
			}
		}
	}
}

void CL_InitDamage (void)
{
	cl_damage_indicators = Cvar_Get ("cl_damage_indicators", "1", CVAR_ARCHIVE);
	cl_hit_markers = Cvar_Get ("cl_hit_markers", "2", CVAR_ARCHIVE);
}
