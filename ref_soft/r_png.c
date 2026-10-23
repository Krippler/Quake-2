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
r_png.c -- PNG images, for the 2023 re-release's true-colour ones.

Every kind the format has: grey, grey with alpha, RGB, RGBA and paletted,
1 to 16 bits a sample, a tRNS chunk's see-through colour or palette alphas,
and Adam7 interlacing. zlib inflates the image data; each row is then
unfiltered and widened to 8-bit RGBA, the top row first. Every length is
checked against the file; anything else is NULL.
*/

#include "r_local.h"
#include <zlib.h>

static unsigned PNG_Long (const byte *p)
{
	return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static int PNG_Paeth (int a, int b, int c)
{
	int	p = a + b - c, pa = abs (p - a), pb = abs (p - b), pc = abs (p - c);

	if (pa <= pb && pa <= pc)
		return a;
	return pb <= pc ? b : c;
}

// undo one row's filter, in place; prev is the row above (unfiltered) or NULL
static qboolean PNG_Unfilter (byte *row, const byte *prev, int len, int bpp, int filter)
{
	int		i, a, b, c;

	for (i=0 ; i<len ; i++)
	{
		a = i >= bpp ? row[i-bpp] : 0;
		b = prev ? prev[i] : 0;
		c = prev && i >= bpp ? prev[i-bpp] : 0;
		switch (filter)
		{
		case 0: break;
		case 1: row[i] += a; break;
		case 2: row[i] += b; break;
		case 3: row[i] += (a + b) >> 1; break;
		case 4: row[i] += PNG_Paeth (a, b, c); break;
		default: return false;
		}
	}
	return true;
}

typedef struct
{
	int		width, height, depth, type, channels;
	byte	palette[256][4];
	int		numpalette;
	int		trns[3];		// grey or RGB that is see-through, -1 for none
} pnginfo_t;

// sample n of a row of samples depth bits wide
static int PNG_Sample (const byte *row, int n, int depth)
{
	switch (depth)
	{
	case 16: return row[n*2];		// the high byte
	case 8: return row[n];
	default:
		return (row[n * depth / 8] >> (8 - depth - (n * depth) % 8)) & ((1 << depth) - 1);
	}
}

// one unfiltered row of w pixels into RGBA at out, every step pixels
static void PNG_Expand (pnginfo_t *pi, const byte *row, int w, byte *out, int step)
{
	int		x, c, v, scale, raw[4];

	scale = pi->depth < 8 && pi->type != 3 ? 255 / ((1 << pi->depth) - 1) : 1;
	for (x=0 ; x<w ; x++, out += step*4)
	{
		for (c=0 ; c<pi->channels ; c++)
			raw[c] = PNG_Sample (row, x*pi->channels + c, pi->depth);
		switch (pi->type)
		{
		case 0:		// grey
			v = raw[0] * scale;
			out[0] = out[1] = out[2] = v;
			out[3] = pi->trns[0] >= 0 && raw[0] == pi->trns[0] >> (pi->depth == 16 ? 8 : 0) ? 0 : 255;
			break;
		case 2:		// RGB
			for (c=0 ; c<3 ; c++)
				out[c] = raw[c];
			out[3] = 255;
			if (pi->trns[0] >= 0)
			{
				int	k = pi->depth == 16 ? 8 : 0;

				if (raw[0] == pi->trns[0] >> k && raw[1] == pi->trns[1] >> k && raw[2] == pi->trns[2] >> k)
					out[3] = 0;
			}
			break;
		case 3:		// paletted
			v = raw[0] < pi->numpalette ? raw[0] : 0;
			for (c=0 ; c<4 ; c++)
				out[c] = pi->palette[v][c];
			break;
		case 4:		// grey and alpha
			out[0] = out[1] = out[2] = raw[0];
			out[3] = raw[1];
			break;
		case 6:		// RGBA
			for (c=0 ; c<4 ; c++)
				out[c] = raw[c];
			break;
		}
	}
}

void LoadPNG (char *name, byte **pic, int *width, int *height)
{
	static const byte	sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	static const int	ax[7] = {0, 4, 0, 2, 0, 1, 0}, ay[7] = {0, 0, 4, 0, 2, 0, 1};
	static const int	dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
	byte		*buffer, *p, *end, *idat = NULL, *raw = NULL, *out = NULL, *row, *prev;
	int			length, idatlen = 0, interlace = 0, i, pass, pw, ph, rowbytes, bpp, y;
	unsigned	len;
	uLongf		rawlen;
	pnginfo_t	pi;

	*pic = NULL;
	length = ri.FS_LoadFile (name, (void **)&buffer);
	if (!buffer)
		return;
	end = buffer + length;
	memset (&pi, 0, sizeof(pi));
	pi.trns[0] = -1;
	for (i=0 ; i<256 ; i++)
		pi.palette[i][3] = 255;

	if (length < 8 + 25 || memcmp (buffer, sig, 8))
		goto bad;

	// the chunks
	for (p = buffer + 8 ; p + 12 <= end ; p += 12 + len)
	{
		len = PNG_Long (p);
		if (len > (unsigned)(end - p - 12))
			goto bad;
		if (!memcmp (p+4, "IHDR", 4) && len >= 13)
		{
			pi.width = PNG_Long (p+8);
			pi.height = PNG_Long (p+12);
			pi.depth = p[16];
			pi.type = p[17];
			interlace = p[20];
			if (p[18] || p[19] || interlace > 1)
				goto bad;
		}
		else if (!memcmp (p+4, "PLTE", 4))
		{
			pi.numpalette = len / 3 > 256 ? 256 : len / 3;
			for (i=0 ; i<pi.numpalette ; i++)
				memcpy (pi.palette[i], p + 8 + i*3, 3);
		}
		else if (!memcmp (p+4, "tRNS", 4))
		{
			if (pi.type == 3)
				for (i=0 ; i<(int)len && i<256 ; i++)
					pi.palette[i][3] = p[8+i];
			else if (pi.type == 0 && len >= 2)
				pi.trns[0] = (p[8] << 8) | p[9];
			else if (pi.type == 2 && len >= 6)
				for (i=0 ; i<3 ; i++)
					pi.trns[i] = (p[8+i*2] << 8) | p[9+i*2];
		}
		else if (!memcmp (p+4, "IDAT", 4))
		{
			byte	*more = realloc (idat, idatlen + len);

			if (!more)
				goto bad;
			idat = more;
			memcpy (idat + idatlen, p + 8, len);
			idatlen += len;
		}
		else if (!memcmp (p+4, "IEND", 4))
			break;
	}

	switch (pi.type)
	{
	case 0: pi.channels = 1; break;
	case 2: pi.channels = 3; break;
	case 3: pi.channels = 1; break;
	case 4: pi.channels = 2; break;
	case 6: pi.channels = 4; break;
	default: goto bad;
	}
	if (pi.width <= 0 || pi.height <= 0 || pi.width > 4096 || pi.height > 4096 || !idat
		|| (pi.depth != 1 && pi.depth != 2 && pi.depth != 4 && pi.depth != 8 && pi.depth != 16)
		|| ((pi.type == 2 || pi.type == 4 || pi.type == 6) && pi.depth < 8)
		|| (pi.type == 3 && pi.depth > 8))
		goto bad;
	bpp = (pi.channels * pi.depth + 7) / 8;

	// how much there is once inflated: a filter byte and the row, each row
	// of each pass
	rawlen = 0;
	for (pass = interlace ? 0 : 6 ; pass < 7 ; pass++)
	{
		pw = interlace ? (pi.width - ax[pass] + dx[pass] - 1) / dx[pass] : pi.width;
		ph = interlace ? (pi.height - ay[pass] + dy[pass] - 1) / dy[pass] : pi.height;
		if (pw > 0 && ph > 0)
			rawlen += (uLongf)ph * (1 + (pw * pi.channels * pi.depth + 7) / 8);
	}
	raw = malloc (rawlen);
	out = calloc (pi.width * pi.height, 4);
	if (!raw || !out || uncompress (raw, &rawlen, idat, idatlen) != Z_OK)
		goto bad;

	row = raw;
	for (pass = interlace ? 0 : 6 ; pass < 7 ; pass++)
	{
		int		x0 = interlace ? ax[pass] : 0, y0 = interlace ? ay[pass] : 0;
		int		sx = interlace ? dx[pass] : 1, sy = interlace ? dy[pass] : 1;

		pw = (pi.width - x0 + sx - 1) / sx;
		ph = (pi.height - y0 + sy - 1) / sy;
		if (pw <= 0 || ph <= 0)
			continue;
		rowbytes = (pw * pi.channels * pi.depth + 7) / 8;
		prev = NULL;
		for (y=0 ; y<ph ; y++)
		{
			if (row + 1 + rowbytes > raw + rawlen)
				goto bad;
			if (!PNG_Unfilter (row + 1, prev, rowbytes, bpp, row[0]))
				goto bad;
			PNG_Expand (&pi, row + 1, pw, out + ((y0 + y*sy) * pi.width + x0) * 4, sx);
			prev = row + 1;
			row += 1 + rowbytes;
		}
	}

	free (idat);
	free (raw);
	ri.FS_FreeFile (buffer);
	*pic = out;
	if (width)
		*width = pi.width;
	if (height)
		*height = pi.height;
	return;

bad:
	ri.Con_Printf (PRINT_DEVELOPER, "Bad png file %s\n", name);
	free (idat);
	free (raw);
	free (out);
	ri.FS_FreeFile (buffer);
}
