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
r_fog.c -- the 2023 re-release's fog, drawn over the finished view.

Its renderer fogs each pixel as it draws it, by the pixel's depth; this one
goes over the view once it is drawn, by the depth the z-buffer has kept
(1/z, by 0x8000), as the re-release's shaders do it (q2pro-ng has them):

  distance fog	f = 1 - exp(-(density/64 * z)^2), towards its colour
  the sky		its colour by the sky factor alone (the sky's depth is
				negative here, R_SkySurf putting it past everything)
  height fog	by how much fog the ray to the pixel goes through between
				the eye's height and the pixel's, from its falloff, and its
				colour from its start colour to its end colour between their
				heights, as the re-release works them out ("very broken",
				q2pro-ng says, but that is how it looks there)

A pixel's colour is the palette's, mixed and taken back to the palette
through the same nearest colours sw_colorlight uses. With distance fog
alone, a table of 64 levels of fog for each of the 256 colours does it,
made again when the fog's colour changes, and the level for each depth a
table too, made again when its density does; a 4x4 dither between the
levels hides the bands the z-buffer's coarse depths far off would leave.
Height fog is worked out for each pixel.
*/

#include "r_local.h"

cvar_t	*sw_fog;

static byte		fog_map[64][256];	// a colour fogged to each level
static float	fog_map_color[3] = {-1, -1, -1};
static byte		fog_sky[256];		// a colour of the sky, fogged
static float	fog_sky_color[4] = {-1, -1, -1, -1};
static unsigned short	fog_level[32768];	// level * 16 for each depth
static float	fog_level_density = -1;
static float	fog_global[32768];	// the distance fog's share at each depth
static float	fog_height[32768];	// and the height fog's, before its extinction
static float	fog_height_density = -1;

static const int fog_dither[4][4] = {
	{ 0, 8, 2,10},
	{12, 4,14, 6},
	{ 3,11, 1, 9},
	{15, 7,13, 5}
};

/*
e^x, near enough for fog (within about 1 in 10000), and several times as
quick as expf for the height fog's two in every pixel: 2^(x log2 e), the
whole power in the float's exponent, the rest by a polynomial
*/
static float R_FogExp (float x)
{
	union { float f; int i; } u;
	float	t, fl, fr;

	if (x < -80)
		return 0;
	if (x > 80)
		x = 80;
	t = x * 1.44269504f;
	fl = floorf (t);
	fr = t - fl;
	u.f = 1 + fr * (0.69583354f + fr * (0.22606716f + fr * 0.07944023f));
	u.i += (int)fl << 23;
	return u.f;
}

static byte R_FogMix (int c, const float *color, float f)
{
	byte	*pal = (byte *)&d_8to24table[c];
	int		rgb[3], i;

	for (i = 0 ; i < 3 ; i++)
	{
		rgb[i] = (int)(pal[i] + (color[i] * 255 - pal[i]) * f + 0.5f);
		if (rgb[i] < 0)
			rgb[i] = 0;
		else if (rgb[i] > 255)
			rgb[i] = 255;
	}
	return R_RGBToPalette (rgb[0], rgb[1], rgb[2]);
}

static void R_FogTables (const float *fog)
{
	int		i, l;
	float	d, f;

	if (fog[FOG_COLOR] != fog_map_color[0] || fog[FOG_COLOR+1] != fog_map_color[1]
		|| fog[FOG_COLOR+2] != fog_map_color[2])
	{
		VectorCopy ((fog + FOG_COLOR), fog_map_color);
		for (i = 0 ; i < 256 ; i++)
			fog_map[0][i] = i;		// none: as it is
		for (l = 1 ; l < 64 ; l++)
			for (i = 0 ; i < 256 ; i++)
				fog_map[l][i] = R_FogMix (i, fog + FOG_COLOR, l / 63.0f);
	}

	if (fog[FOG_DENSITY] != fog_level_density)
	{
		fog_level_density = fog[FOG_DENSITY];
		fog_level[0] = 0;
		for (i = 1 ; i < 32768 ; i++)
		{
			d = fog_level_density / 64 * (32768.0f / i);
			f = 1 - expf (-(d * d));
			fog_level[i] = (unsigned short)(f * 63 * 16);
			fog_global[i] = f;
		}
	}

	if (fog[FOG_HF_DENSITY] != fog_height_density)
	{
		fog_height_density = fog[FOG_HF_DENSITY];
		fog_height[0] = 0;
		for (i = 1 ; i < 32768 ; i++)
			fog_height[i] = 1 - expf (-fog_height_density * (32768.0f / i));
	}

	if (fog[FOG_SKY_FACTOR] != fog_sky_color[3] || fog[FOG_COLOR] != fog_sky_color[0]
		|| fog[FOG_COLOR+1] != fog_sky_color[1] || fog[FOG_COLOR+2] != fog_sky_color[2])
	{
		VectorCopy ((fog + FOG_COLOR), fog_sky_color);
		fog_sky_color[3] = fog[FOG_SKY_FACTOR];
		for (i = 0 ; i < 256 ; i++)
			fog_sky[i] = fog_sky_color[3] > 0 ? R_FogMix (i, fog + FOG_COLOR, fog_sky_color[3]) : i;
	}
}

/*
The height fog's extinction and how far from its start colour to its end
colour, for the pixel at x, y at depth zb, as the re-release works them out
*/
static float	hf_falloff, hf_start, hf_end, hf_eye_exp;
static qboolean	hf_global;

static void R_HeightFogAt (int x, int y, int zb, float *ext, float *frac)
{
	vec3_t	dir;
	float	z = 32768.0f / zb, len, dir_z, pos, hd, e, fr;

	// the ray through the pixel, for a depth of 1
	VectorMA (vpn, (ycenter - y) / yscale, vup, dir);
	VectorMA (dir, (x - xcenter) / xscale, vright, dir);
	len = VectorLength (dir);
	dir_z = len > 0 ? dir[2] / len : 0;
	if (dir_z == 0)
		dir_z = 0.00001f;
	pos = r_refdef.vieworg[2] + dir[2] * z - hf_start;
	hd = (hf_eye_exp - R_FogExp (-hf_falloff * pos)) / (hf_falloff * dir_z);
	e = 1 - R_FogExp (-hd);
	if (!(e > 0))		// and not a number, far under its start
		e = 0;
	else if (e > 1)
		e = 1;
	fr = hf_end != hf_start ? (pos - hf_start) / (hf_end - hf_start) : 0;
	if (fr < 0)
		fr = 0;
	else if (fr > 1)
		fr = 1;
	*ext = e;
	*frac = fr;
}

// a pixel fogged, both fogs, and taken back to the palette
static void R_FogPixel (pixel_t *pix, int x, int y, int zb, float ext, float frac)
{
	const float	*fog = r_newrefdef.fog;
	byte	*pal = (byte *)&d_8to24table[*pix];
	float	rgb[3], f = 0, hd, c;
	int		i, out[3];

	rgb[0] = pal[0];
	rgb[1] = pal[1];
	rgb[2] = pal[2];

	if (hf_global)
	{
		f = fog_global[zb];
		for (i = 0 ; i < 3 ; i++)
			rgb[i] += (fog[FOG_COLOR+i] * 255 - rgb[i]) * f;
	}

	hd = fog_height[zb] * ext;
	for (i = 0 ; i < 3 ; i++)
	{
		c = (fog[FOG_HF_START_COLOR+i] + (fog[FOG_HF_END_COLOR+i] - fog[FOG_HF_START_COLOR+i]) * frac) * ext * 255;
		rgb[i] += (c - rgb[i]) * hd;
	}
	f += hd;

	// too little to see: left as it was, as the palette would not give
	// it back
	if (f < 1 / 64.0f)
		return;

	for (i = 0 ; i < 3 ; i++)
	{
		out[i] = (int)(rgb[i] + (fog_dither[y & 3][x & 3] - 7.5f) * 0.25f + 0.5f);
		if (out[i] < 0)
			out[i] = 0;
		else if (out[i] > 255)
			out[i] = 255;
	}
	*pix = R_RGBToPalette (out[0], out[1], out[2]);
}

/*
================
R_ApplyFog

Over the view just drawn, before the underwater warp
================
*/
void R_ApplyFog (void)
{
	const float	*fog = r_newrefdef.fog;
	qboolean	global, height, sky;
	int			x, y, x0, y0, x1, y1, l, zb;
	pixel_t		*pix;
	short		*pz;

	if (!sw_fog->value || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		return;

	global = fog[FOG_DENSITY] > 0;
	height = fog[FOG_HF_DENSITY] > 0 && fog[FOG_HF_FALLOFF] > 0;
	sky = fog[FOG_SKY_FACTOR] > 0;
	if (!global && !height && !sky)
		return;

	R_FogTables (fog);

	x0 = r_refdef.vrect.x;
	y0 = r_refdef.vrect.y;
	x1 = x0 + r_refdef.vrect.width;
	y1 = y0 + r_refdef.vrect.height;

	if (!height)
	{
		for (y = y0 ; y < y1 ; y++)
		{
			pix = d_viewbuffer + y * r_screenwidth + x0;
			pz = d_pzbuffer + y * d_zwidth + x0;
			for (x = x0 ; x < x1 ; x++, pix++, pz++)
			{
				zb = *pz;
				if (zb <= 0)
				{
					*pix = fog_sky[*pix];
					continue;
				}
				if (!global)
					continue;
				l = (fog_level[zb] + fog_dither[y & 3][x & 3]) >> 4;
				if (l > 63)
					l = 63;
				*pix = fog_map[l][*pix];
			}
		}
		return;
	}

	// height fog: its extinction and colour for each 2x2 block of pixels
	// (worked out again for a pixel of the block much nearer or farther
	// than the first), its density by each pixel's own depth
	{
		int		bx, by, zr;
		float	ext = 0, frac = 0;

		hf_falloff = fog[FOG_HF_FALLOFF];
		hf_start = fog[FOG_HF_START_DIST];
		hf_end = fog[FOG_HF_END_DIST];
		hf_eye_exp = R_FogExp (-hf_falloff * (r_refdef.vieworg[2] - hf_start));
		hf_global = global;

		for (by = y0 ; by < y1 ; by += 2)
			for (bx = x0 ; bx < x1 ; bx += 2)
			{
				zr = 0;
				for (y = by ; y < by + 2 && y < y1 ; y++)
					for (x = bx ; x < bx + 2 && x < x1 ; x++)
					{
						pix = d_viewbuffer + y * r_screenwidth + x;
						zb = d_pzbuffer[y * d_zwidth + x];
						if (zb <= 0)
						{
							*pix = fog_sky[*pix];
							continue;
						}
						if (!zr || abs (zb - zr) * 8 > zr)
						{
							R_HeightFogAt (x, y, zb, &ext, &frac);
							if (!zr)
								zr = zb;
						}
						R_FogPixel (pix, x, y, zb, ext, frac);
					}
			}
	}
}

