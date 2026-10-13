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

// draw.c

#include "r_local.h"


image_t		*draw_chars;				// 8*8 graphic characters

/*
The 2D layer -- menus, console, status bar, crosshair -- was drawn a screen
pixel to a pixel of its art, which at 320x240 filled the screen and at
1920x1200 is a postage stamp in the middle of it. It is drawn at a whole
multiple instead: the client is told the screen is r_vw x r_vh, lays
everything out in those units as it always has, and each Draw_ call and the
3D view's rectangle are multiplied up by r_scale here.

scr_scale 0 picks the largest multiple that leaves at least 640x400 of
room, so 1 up to 1279 wide, 2 at 1280x720 or 1600x1200, 3 at 1920x1200.
1 is id's.
*/
int			r_scale = 1;
int			r_vw, r_vh;

void R_NewWindow (void)
{
	int		s;

	s = (int)scr_scale->value;
	if (s <= 0)
	{
		s = vid.width / 640;
		if (vid.height / 400 < s)
			s = vid.height / 400;
	}
	// the menus are laid out for 320x240, and need it
	while (s > 1 && (vid.width / s < 320 || vid.height / s < 240))
		s--;
	if (s < 1)
		s = 1;

	r_scale = s;
	r_vw = vid.width / s;
	r_vh = vid.height / s;
	ri.Vid_NewWindow (r_vw, r_vh);
}

/*
A rectangle in the client's units, in screen pixels. An edge on the right
or bottom of the client's screen goes to the real one's, which is up to
r_scale-1 pixels further when the scale does not divide the screen evenly.
*/
void R_ScaleRect (int *x, int *y, int *w, int *h)
{
	int		x1, y1;

	if (r_scale == 1)
		return;
	x1 = *x + *w;
	y1 = *y + *h;
	x1 = x1 >= r_vw ? vid.width : x1 * r_scale;
	y1 = y1 >= r_vh ? vid.height : y1 * r_scale;
	*x *= r_scale;
	*y *= r_scale;
	*w = x1 - *x;
	*h = y1 - *y;
}

/*
w x h pixels of art, stride apart, at x,y in the client's units, each one
r_scale pixels square, clipped to the screen.
*/
static void Draw_Scaled (int x, int y, byte *source, int w, int h, int stride,
		qboolean transparent)
{
	int		s = r_scale;
	int		x0, y0, x1, y1, px, py, c;
	byte	*row, *dest;

	x0 = x * s;
	y0 = y * s;
	x1 = (x + w) * s;
	y1 = (y + h) * s;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > vid.width)
		x1 = vid.width;
	if (y1 > vid.height)
		y1 = vid.height;

	for (py = y0 ; py < y1 ; py++)
	{
		row = source + (py - y * s) / s * stride;
		dest = vid.buffer + py * vid.rowbytes;
		for (px = x0 ; px < x1 ; px++)
		{
			c = row[(px - x * s) / s];
			if (!transparent || c != TRANSPARENT_COLOR)
				dest[px] = c;
		}
	}
}

//=============================================================================

/*
================
Draw_FindPic
================
*/
image_t *Draw_FindPic (char *name)
{
	image_t	*image;
	char	fullname[MAX_QPATH];

	if (name[0] != '/' && name[0] != '\\')
	{
		Com_sprintf (fullname, sizeof(fullname), "pics/%s.pcx", name);
		image = R_FindImage (fullname, it_pic);
	}
	else
		image = R_FindImage (name+1, it_pic);

	return image;
}



/*
===============
Draw_InitLocal
===============
*/
void Draw_InitLocal (void)
{
	draw_chars = Draw_FindPic ("conchars");

	// Every Quake II pak0 has the console font. One without it is not the
	// game this engine plays -- the 2023 re-release's is the one met in
	// practice, whose maps the 1997 network protocol cannot carry either --
	// and every message after this would be drawn with it.
	if (!draw_chars)
		ri.Sys_Error (ERR_FATAL, "pics/conchars.pcx is missing, so this is not the "
			"original Quake II's data. The 2023 re-release's own pak files are "
			"for its own engine; the original game's baseq2 is beside its "
			"rerelease folder in the same install, and that is the one to use");
}



/*
================
Draw_Char

Draws one 8*8 graphics character
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Char (int x, int y, int num)
{
	byte			*dest;
	byte			*source;
	int				drawline;	
	int				row, col;

	num &= 255;

	if (num == 32 || num == 32+128 || !draw_chars)
		return;

	if (y <= -8)
		return;			// totally off screen

//	if ( ( y + 8 ) >= vid.height )
	if ( ( y + 8 ) > r_vh )		// PGM - status text was missing in sw...
		return;

	if (r_scale > 1)
	{
		Draw_Scaled (x, y, draw_chars->pixels[0] + ((num>>4)<<10) + ((num&15)<<3),
			8, 8, 128, true);
		return;
	}

#ifdef PARANOID
	if (y > vid.height - 8 || x < 0 || x > vid.width - 8)
		ri.Sys_Error (ERR_FATAL,"Con_DrawCharacter: (%i, %i)", x, y);
	if (num < 0 || num > 255)
		ri.Sys_Error (ERR_FATAL,"Con_DrawCharacter: char %i", num);
#endif

	row = num>>4;
	col = num&15;
	source = draw_chars->pixels[0] + (row<<10) + (col<<3);

	if (y < 0)
	{	// clipped
		drawline = 8 + y;
		source -= 128*y;
		y = 0;
	}
	else
		drawline = 8;


	dest = vid.buffer + y*vid.rowbytes + x;

	while (drawline--)
	{
		if (source[0] != TRANSPARENT_COLOR)
			dest[0] = source[0];
		if (source[1] != TRANSPARENT_COLOR)
			dest[1] = source[1];
		if (source[2] != TRANSPARENT_COLOR)
			dest[2] = source[2];
		if (source[3] != TRANSPARENT_COLOR)
			dest[3] = source[3];
		if (source[4] != TRANSPARENT_COLOR)
			dest[4] = source[4];
		if (source[5] != TRANSPARENT_COLOR)
			dest[5] = source[5];
		if (source[6] != TRANSPARENT_COLOR)
			dest[6] = source[6];
		if (source[7] != TRANSPARENT_COLOR)
			dest[7] = source[7];
		source += 128;
		dest += vid.rowbytes;
	}
}

/*
=============
Draw_GetPicSize
=============
*/
void Draw_GetPicSize (int *w, int *h, char *pic)
{
	image_t *gl;

	gl = Draw_FindPic (pic);
	if (!gl)
	{
		*w = *h = -1;
		return;
	}
	*w = gl->width;
	*h = gl->height;
}

/*
=============
Draw_StretchPicImplementation
=============
*/
void Draw_StretchPicImplementation (int x, int y, int w, int h, image_t	*pic)
{
	byte			*dest, *source;
	int				v, u, sv;
	int				height;
	int				f, fstep;
	int				skip;

	if ((x < 0) ||
		(x + w > vid.width) ||
		(y + h > vid.height))
	{
		// a frame laid out before the scale or the mode changed
		if (r_scale > 1 || x + w > vid.width || y + h > vid.height)
			return;
		ri.Sys_Error (ERR_FATAL,"Draw_Pic: bad coordinates");
	}

	height = h;
	if (y < 0)
	{
		skip = -y;
		height += y;
		y = 0;
	}
	else
		skip = 0;

	dest = vid.buffer + y * vid.rowbytes + x;

	for (v=0 ; v<height ; v++, dest += vid.rowbytes)
	{
		sv = (skip + v)*pic->height/h;
		source = pic->pixels[0] + sv*pic->width;
		if (w == pic->width)
			memcpy (dest, source, w);
		else
		{
			f = 0;
			fstep = pic->width*0x10000/w;
			for (u=0 ; u<w ; u+=4)
			{
				dest[u] = source[f>>16];
				f += fstep;
				dest[u+1] = source[f>>16];
				f += fstep;
				dest[u+2] = source[f>>16];
				f += fstep;
				dest[u+3] = source[f>>16];
				f += fstep;
			}
		}
	}
}

/*
=============
Draw_StretchPic
=============
*/
void Draw_StretchPic (int x, int y, int w, int h, char *name)
{
	image_t	*pic;

	pic = Draw_FindPic (name);
	if (!pic)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}
	R_ScaleRect (&x, &y, &w, &h);
	Draw_StretchPicImplementation (x, y, w, h, pic);
}

/*
=============
Draw_StretchRaw
=============
*/
void Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data)
{
	image_t	pic;

	pic.pixels[0] = data;
	pic.width = cols;
	pic.height = rows;
	R_ScaleRect (&x, &y, &w, &h);
	Draw_StretchPicImplementation (x, y, w, h, &pic);
}

/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, char *name)
{
	image_t			*pic;
	byte			*dest, *source;
	int				v, u;
	int				tbyte;
	int				height;

	pic = Draw_FindPic (name);
	if (!pic)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}

	if ((x < 0) ||
		(x + pic->width > r_vw) ||
		(y + pic->height > r_vh))
		return;	//	ri.Sys_Error (ERR_FATAL,"Draw_Pic: bad coordinates");

	if (r_scale > 1)
	{
		Draw_Scaled (x, y, pic->pixels[0], pic->width, pic->height,
			pic->width, pic->transparent);
		return;
	}

	height = pic->height;
	source = pic->pixels[0];
	if (y < 0)
	{
		height += y;
		source += pic->width*-y;
		y = 0;
	}

	dest = vid.buffer + y * vid.rowbytes + x;

	if (!pic->transparent)
	{
		for (v=0 ; v<height ; v++)
		{
			memcpy (dest, source, pic->width);
			dest += vid.rowbytes;
			source += pic->width;
		}
	}
	else
	{
		if (pic->width & 7)
		{	// general
			for (v=0 ; v<height ; v++)
			{
				for (u=0 ; u<pic->width ; u++)
					if ( (tbyte=source[u]) != TRANSPARENT_COLOR)
						dest[u] = tbyte;

				dest += vid.rowbytes;
				source += pic->width;
			}
		}
		else
		{	// unwound
			for (v=0 ; v<height ; v++)
			{
				for (u=0 ; u<pic->width ; u+=8)
				{
					if ( (tbyte=source[u]) != TRANSPARENT_COLOR)
						dest[u] = tbyte;
					if ( (tbyte=source[u+1]) != TRANSPARENT_COLOR)
						dest[u+1] = tbyte;
					if ( (tbyte=source[u+2]) != TRANSPARENT_COLOR)
						dest[u+2] = tbyte;
					if ( (tbyte=source[u+3]) != TRANSPARENT_COLOR)
						dest[u+3] = tbyte;
					if ( (tbyte=source[u+4]) != TRANSPARENT_COLOR)
						dest[u+4] = tbyte;
					if ( (tbyte=source[u+5]) != TRANSPARENT_COLOR)
						dest[u+5] = tbyte;
					if ( (tbyte=source[u+6]) != TRANSPARENT_COLOR)
						dest[u+6] = tbyte;
					if ( (tbyte=source[u+7]) != TRANSPARENT_COLOR)
						dest[u+7] = tbyte;
				}
				dest += vid.rowbytes;
				source += pic->width;
			}
		}
	}
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h, char *name)
{
	int			i, j;
	byte		*psrc;
	byte		*pdest;
	image_t		*pic;
	int			x2;

	R_ScaleRect (&x, &y, &w, &h);
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (x + w > vid.width)
		w = vid.width - x;
	if (y + h > vid.height)
		h = vid.height - y;
	if (w <= 0 || h <= 0)
		return;

	pic = Draw_FindPic (name);
	if (!pic)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}
	x2 = x + w;
	pdest = vid.buffer + y*vid.rowbytes;
	for (i=0 ; i<h ; i++, pdest += vid.rowbytes)
	{
		psrc = pic->pixels[0] + pic->width * (((i+y)/r_scale)&63);
		for (j=x ; j<x2 ; j++)
			pdest[j] = psrc[(j/r_scale)&63];
	}
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	byte			*dest;
	int				u, v;

	R_ScaleRect (&x, &y, &w, &h);
	if (x+w > vid.width)
		w = vid.width - x;
	if (y+h > vid.height)
		h = vid.height - y;
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (w < 0 || h < 0)
		return;
	dest = vid.buffer + y*vid.rowbytes + x;
	for (v=0 ; v<h ; v++, dest += vid.rowbytes)
		for (u=0 ; u<w ; u++)
			dest[u] = c;
}
//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
	int			x,y;
	byte		*pbuf;
	int	t;

	for (y=0 ; y<vid.height ; y++)
	{
		pbuf = (byte *)(vid.buffer + vid.rowbytes*y);
		t = (y & 1) << 1;

		for (x=0 ; x<vid.width ; x++)
		{
			if ((x & 3) != t)
				pbuf[x] = 0;
		}
	}
}
