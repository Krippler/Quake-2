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

#include "r_local.h"


#define	MAX_RIMAGES	1024
image_t		r_images[MAX_RIMAGES];
int			numr_images;


/*
===============
R_ImageList_f
===============
*/
void	R_ImageList_f (void)
{
	int		i;
	image_t	*image;
	int		texels;

	ri.Con_Printf (PRINT_ALL, "------------------\n");
	texels = 0;

	for (i=0, image=r_images ; i<numr_images ; i++, image++)
	{
		if (image->registration_sequence <= 0)
			continue;
		texels += image->width*image->height;
		switch (image->type)
		{
		case it_skin:
			ri.Con_Printf (PRINT_ALL, "M");
			break;
		case it_sprite:
			ri.Con_Printf (PRINT_ALL, "S");
			break;
		case it_wall:
			ri.Con_Printf (PRINT_ALL, "W");
			break;
		case it_pic:
			ri.Con_Printf (PRINT_ALL, "P");
			break;
		default:
			ri.Con_Printf (PRINT_ALL, " ");
			break;
		}

		ri.Con_Printf (PRINT_ALL,  " %3i %3i : %s\n",
			image->width, image->height, image->name);
	}
	ri.Con_Printf (PRINT_ALL, "Total texel count: %i\n", texels);
}


/*
=================================================================

PCX LOADING

=================================================================
*/

/*
=================
R_LoadUsable

ri.FS_LoadFile, for the first copy of name down the search path that
usable() (qcommon/usable.c) accepts: past a 24-bit picture or an extended
map in the 2023 re-release's pak, to the original game's copy underneath.
Pictures and textures say so only with developer 1, since a re-release pak
can pass over hundreds of them; models and maps always do.
=================
*/
int R_LoadUsable (char *name, void **buf,
	qboolean (*usable) (const byte *buf, int len, char *why, int whysize), qboolean loud)
{
	int		skip, len;
	char	why[128];

	for (skip = 0 ; ; skip++)
	{
		len = ri.FS_LoadFileSkip (name, buf, skip);
		if (!*buf || usable (*buf, len, why, sizeof(why)))
			return len;
		ri.Con_Printf (loud ? PRINT_ALL : PRINT_DEVELOPER,
			"%s %s; looking further down the search path\n", name, why);
		ri.FS_FreeFile (*buf);
	}
}

/*
==============
LoadPCX
==============
*/
void LoadPCX (char *filename, byte **pic, byte **palette, int *width, int *height)
{
	byte	*raw;
	pcx_t	*pcx;
	int		x, y;
	int		len;
	int		dataByte, runLength;
	byte	*out, *pix;

	*pic = NULL;

	//
	// load the file
	//
	len = R_LoadUsable (filename, (void **)&raw, PCX_Usable, false);
	if (!raw)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "Bad pcx file %s\n", filename);
		return;
	}

	//
	// parse the PCX file
	//
	pcx = (pcx_t *)raw;

    pcx->xmin = LittleShort(pcx->xmin);
    pcx->ymin = LittleShort(pcx->ymin);
    pcx->xmax = LittleShort(pcx->xmax);
    pcx->ymax = LittleShort(pcx->ymax);
    pcx->hres = LittleShort(pcx->hres);
    pcx->vres = LittleShort(pcx->vres);
    pcx->bytes_per_line = LittleShort(pcx->bytes_per_line);
    pcx->palette_type = LittleShort(pcx->palette_type);

	raw = &pcx->data;

	if (pcx->manufacturer != 0x0a
		|| pcx->version != 5
		|| pcx->encoding != 1
		|| pcx->bits_per_pixel != 8
		|| pcx->xmax >= 640
		|| pcx->ymax >= 480)
	{
		ri.Con_Printf (PRINT_ALL, "Bad pcx file %s\n", filename);
		return;
	}

	out = malloc ( (pcx->ymax+1) * (pcx->xmax+1) );

	*pic = out;

	pix = out;

	if (palette)
	{
		*palette = malloc(768);
		memcpy (*palette, (byte *)pcx + len - 768, 768);
	}

	if (width)
		*width = pcx->xmax+1;
	if (height)
		*height = pcx->ymax+1;

	for (y=0 ; y<=pcx->ymax ; y++, pix += pcx->xmax+1)
	{
		for (x=0 ; x<=pcx->xmax ; )
		{
			dataByte = *raw++;

			if((dataByte & 0xC0) == 0xC0)
			{
				runLength = dataByte & 0x3F;
				dataByte = *raw++;
			}
			else
				runLength = 1;

			while(runLength-- > 0)
				pix[x++] = dataByte;
		}

	}

	if ( raw - (byte *)pcx > len)
	{
		ri.Con_Printf (PRINT_DEVELOPER, "PCX file %s was malformed", filename);
		free (*pic);
		*pic = NULL;
	}

	ri.FS_FreeFile (pcx);
}

/*
=========================================================

TARGA LOADING, AND THE RE-RELEASE'S TRUE-COLOUR IMAGES

The 2023 re-release keeps an image the original game has as an 8-bit .pcx
or .wal in that form, but one of its own -- the mission packs' skies when
their demos play from baseq2, every texture of the Quake 64 maps -- only
as a 24- or 32-bit .tga. Asked for a .pcx or .wal that is not there, the
renderer takes the .tga of the same name instead, in the palette's colours
(R_RGBToPalette, the nearest of those the colormap shades), at a size the
software renderer can draw: a sky at 256x256 as the originals are, a
texture a multiple of 16 on each side and with its four mip levels.

Failing a .tga, a .png of the same name (r_png.c). Most of the re-release's
are its own engine's -- menus, glow maps, the skins of its MD5 models -- and
are never asked for; the rest are found as the targas are.
=========================================================
*/

/*
=============
LoadTGA

An uncompressed or run-length 24- or 32-bit targa into RGBA, top row first.
NULL for anything else, or for a file shorter than it says it is.
=============
*/
void LoadTGA (char *name, byte **pic, int *width, int *height)
{
	byte	*buffer, *p, *end, *out;
	int		length, type, bpp, w, h, flip, row, col, i, n, rle, count, grey;
	byte	c[4];

	*pic = NULL;
	length = ri.FS_LoadFile (name, (void **)&buffer);
	if (!buffer)
		return;
	end = buffer + length;
	if (length < 18)
		goto bad;

	type = buffer[2];
	w = buffer[12] | (buffer[13] << 8);
	h = buffer[14] | (buffer[15] << 8);
	bpp = buffer[16];
	flip = !(buffer[17] & 0x20);	// rows bottom first, the usual way
	// colour (2, 10 run-length), or grey (3, 11), as the re-release's
	// flares are
	grey = (type == 3 || type == 11);
	if ((type != 2 && type != 10 && !grey) || buffer[1] != 0
		|| (grey ? (bpp != 8 && bpp != 16) : (bpp != 24 && bpp != 32))
		|| w <= 0 || h <= 0 || w > 4096 || h > 4096)
		goto bad;
	bpp >>= 3;
	if (type == 11)
		type = 10;		// the same packets, of grey
	else if (type == 3)
		type = 2;
	p = buffer + 18 + buffer[0];

	out = malloc (w*h*4);
	if (!out)
		goto bad;
	rle = 0;
	count = 0;
	for (row=0 ; row<h ; row++)
	{
		byte	*dst = out + (flip ? h-1-row : row) * w * 4;

		for (col=0 ; col<w ; col++)
		{
			if (type == 2 || (!rle && !count))
			{
				if (type == 10)
				{
					// a packet: a run of one colour, or raw colours
					if (p >= end)
						goto badout;
					n = *p++;
					rle = n & 0x80;
					count = (n & 0x7f) + 1;
				}
				if (p + bpp > end)
					goto badout;
				if (grey)
				{
					c[0] = c[1] = c[2] = p[0];
					c[3] = bpp == 2 ? p[1] : 255;
				}
				else
				{
					c[2] = p[0]; c[1] = p[1]; c[0] = p[2];
					c[3] = bpp == 4 ? p[3] : 255;
				}
				p += bpp;
			}
			else if (!rle)
			{
				if (p + bpp > end)
					goto badout;
				if (grey)
				{
					c[0] = c[1] = c[2] = p[0];
					c[3] = bpp == 2 ? p[1] : 255;
				}
				else
				{
					c[2] = p[0]; c[1] = p[1]; c[0] = p[2];
					c[3] = bpp == 4 ? p[3] : 255;
				}
				p += bpp;
			}
			for (i=0 ; i<4 ; i++)
				dst[col*4+i] = c[i];
			if (type == 10 && --count == 0)
				rle = 0;
		}
	}

	ri.FS_FreeFile (buffer);
	*pic = out;
	if (width)
		*width = w;
	if (height)
		*height = h;
	return;

badout:
	free (out);
bad:
	ri.Con_Printf (PRINT_DEVELOPER, "Bad tga file %s\n", name);
	ri.FS_FreeFile (buffer);
}

/*
=============
R_ResampleRGBA

RGBA at w x h to nw x nh: the average of the source pixels each covers.
=============
*/
static byte *R_ResampleRGBA (byte *in, int w, int h, int nw, int nh)
{
	byte	*out = malloc (nw*nh*4);
	int		x, y, sx, sy, x0, x1, y0, y1, c, n;
	unsigned	sum[4];

	if (!out)
		return NULL;
	for (y=0 ; y<nh ; y++)
	{
		y0 = y * h / nh;
		y1 = (y+1) * h / nh;
		if (y1 <= y0)
			y1 = y0 + 1;
		for (x=0 ; x<nw ; x++)
		{
			x0 = x * w / nw;
			x1 = (x+1) * w / nw;
			if (x1 <= x0)
				x1 = x0 + 1;
			sum[0] = sum[1] = sum[2] = sum[3] = 0;
			n = 0;
			for (sy=y0 ; sy<y1 ; sy++)
				for (sx=x0 ; sx<x1 ; sx++, n++)
					for (c=0 ; c<4 ; c++)
						sum[c] += in[(sy*w + sx)*4 + c];
			for (c=0 ; c<4 ; c++)
				out[(y*nw + x)*4 + c] = sum[c] / n;
		}
	}
	return out;
}

// RGBA to the palette; with alpha, a mostly clear pixel to 255, see-through
static void R_QuantizeRGBA (byte *in, byte *out, int count, qboolean alpha)
{
	int		i;

	for (i=0 ; i<count ; i++, in+=4)
		out[i] = alpha && in[3] < 128 ? 255 : R_RGBToPalette (in[0], in[1], in[2]);
}

/*
=============
R_LoadTrueColor

The re-release's .tga for name, a .pcx or .wal that is not there; NULL if
it has none either.
=============
*/
image_t *R_FindFreeImage (void);

static image_t *R_LoadTrueColor (char *name, imagetype_t type)
{
	char	tganame[MAX_QPATH];
	byte	*rgba, *sized, *level, *next;
	int		w, h, nw, nh, len, i, size;
	image_t	*image;

	len = strlen (name);
	if (len < 5 || len >= MAX_QPATH)
		return NULL;
	memcpy (tganame, name, len - 4);
	strcpy (tganame + len - 4, ".tga");
	LoadTGA (tganame, &rgba, &w, &h);
	if (!rgba)
	{
		strcpy (tganame + len - 4, ".png");
		LoadPNG (tganame, &rgba, &w, &h);
	}
	if (!rgba)
		return NULL;

	// a size the renderer can draw
	nw = w;
	nh = h;
	if (type == it_sky)
		nw = nh = 256;
	else if (type == it_wall)
	{
		nw = (w + 8) & ~15;
		nh = (h + 8) & ~15;
		if (nw < 16)
			nw = 16;
		if (nh < 16)
			nh = 16;
	}
	sized = rgba;
	if (nw != w || nh != h)
	{
		sized = R_ResampleRGBA (rgba, w, h, nw, nh);
		free (rgba);
		if (!sized)
			return NULL;
	}

	image = R_FindFreeImage ();
	strcpy (image->name, name);
	image->width = nw;
	image->height = nh;
	image->type = type;
	image->registration_sequence = registration_sequence;
	image->transparent = false;

	if (type == it_wall)
	{
		// four mip levels, each the average of the last, as a .wal has
		size = nw*nh * (256+64+16+4)/256;
		image->pixels[0] = malloc (size);
		image->pixels[1] = image->pixels[0] + nw*nh;
		image->pixels[2] = image->pixels[1] + nw*nh/4;
		image->pixels[3] = image->pixels[2] + nw*nh/16;
		level = sized;
		for (i=0 ; i<4 ; i++)
		{
			R_QuantizeRGBA (level, image->pixels[i], (nw>>i)*(nh>>i), false);
			if (i == 3)
				break;
			next = R_ResampleRGBA (level, nw>>i, nh>>i, nw>>(i+1), nh>>(i+1));
			if (level != sized)
				free (level);
			if (!next)
			{
				// what is left from the level above, rather than nothing
				for (i++ ; i<4 ; i++)
					memset (image->pixels[i], image->pixels[0][0], (nw>>i)*(nh>>i));
				level = sized;
				break;
			}
			level = next;
		}
		if (level != sized)
			free (level);
	}
	else
	{
		image->pixels[0] = R_AllocPixels (image, nw*nh);
		R_QuantizeRGBA (sized, image->pixels[0], nw*nh, type != it_sky);
		for (i=0 ; i<nw*nh ; i++)
			if (image->pixels[0][i] == 255)
				image->transparent = true;
	}
	free (sized);

	ri.Con_Printf (PRINT_DEVELOPER, "%s: from %s, %dx%d\n", name, tganame, nw, nh);
	return image;
}


//=======================================================

image_t *R_FindFreeImage (void)
{
	image_t		*image;
	int			i;

	// find a free image_t
	for (i=0, image=r_images ; i<numr_images ; i++,image++)
	{
		if (!image->registration_sequence)
			break;
	}
	if (i == numr_images)
	{
		if (numr_images == MAX_RIMAGES)
			ri.Sys_Error (ERR_DROP, "MAX_RIMAGES");
		numr_images++;
	}
	image = &r_images[i];

	return image;
}

/*
================
R_AllocPixels, R_FreePixels

A skin's pixels with room on either side of them: the alias model
drawing steps a texture pointer along a span, and where a triangle meets
the edge of the screen it can land a pixel or so outside the skin -- a
read of what lies next to it, as in 1997, which ASan catches. Two rows and
some either side keep that inside what is allocated.
================
*/
byte *R_AllocPixels (image_t *image, int size)
{
	byte	*p;

	image->pad = image->type == it_skin ? image->width * 2 + 256 : 0;
	p = malloc (size + 2 * image->pad);
	if (!p)
		ri.Sys_Error (ERR_FATAL, "R_AllocPixels: out of memory");
	memset (p, 0, image->pad);
	memset (p + image->pad + size, 0, image->pad);
	return p + image->pad;
}

void R_FreePixels (image_t *image)
{
	if (image->pixels[0])
		free (image->pixels[0] - image->pad);
	image->pixels[0] = NULL;
}

/*
================
GL_LoadPic

================
*/
image_t *GL_LoadPic (char *name, byte *pic, int width, int height, imagetype_t type)
{
	image_t		*image;
	int			i, c, b;

	image = R_FindFreeImage ();
	if (strlen(name) >= sizeof(image->name))
		ri.Sys_Error (ERR_DROP, "Draw_LoadPic: \"%s\" is too long", name);
	strcpy (image->name, name);
	image->registration_sequence = registration_sequence;

	image->width = width;
	image->height = height;
	image->type = type;

	c = width*height;
	image->pixels[0] = R_AllocPixels (image, c);
	image->transparent = false;
	for (i=0 ; i<c ; i++)
	{
		b = pic[i];
		if (b == 255)
			image->transparent = true;
		image->pixels[0][i] = b;
	}

	return image;
}

/*
================
R_LoadWal
================
*/
image_t *R_LoadWal (char *name)
{
	miptex_t	*mt;
	int			ofs;
	image_t		*image;
	int			size;

	R_LoadUsable (name, (void **)&mt, WAL_Usable, false);
	if (!mt)
	{
		if ((image = R_LoadTrueColor (name, it_wall)) != NULL)
			return image;
		ri.Con_Printf (PRINT_ALL, "R_LoadWal: can't load %s\n", name);
		return r_notexture_mip;
	}

	image = R_FindFreeImage ();
	strcpy (image->name, name);
	image->width = LittleLong (mt->width);
	image->height = LittleLong (mt->height);
	image->type = it_wall;
	image->registration_sequence = registration_sequence;

	size = image->width*image->height * (256+64+16+4)/256;
	image->pixels[0] = malloc (size);
	image->pixels[1] = image->pixels[0] + image->width*image->height;
	image->pixels[2] = image->pixels[1] + image->width*image->height/4;
	image->pixels[3] = image->pixels[2] + image->width*image->height/16;

	ofs = LittleLong (mt->offsets[0]);
	memcpy ( image->pixels[0], (byte *)mt + ofs, size);

	ri.FS_FreeFile ((void *)mt);

	return image;
}


/*
===============
R_FindImage

Finds or loads the given image
===============
*/
image_t	*R_FindImage (char *name, imagetype_t type)
{
	image_t	*image;
	int		i, len;
	byte	*pic, *palette;
	int		width, height;

	if (!name)
		return NULL;	// ri.Sys_Error (ERR_DROP, "R_FindImage: NULL name");
	len = strlen(name);
	if (len<5)
		return NULL;	// ri.Sys_Error (ERR_DROP, "R_FindImage: bad name: %s", name);

	// look for it
	for (i=0, image=r_images ; i<numr_images ; i++,image++)
	{
		if (!strcmp(name, image->name))
		{
			image->registration_sequence = registration_sequence;
			return image;
		}
	}

	//
	// load the pic from disk
	//
	pic = NULL;
	palette = NULL;
	if (!strcmp(name+len-4, ".pcx"))
	{
		LoadPCX (name, &pic, &palette, &width, &height);
		if (!pic)
			return R_LoadTrueColor (name, type);
		image = GL_LoadPic (name, pic, width, height, type);
	}
	else if (!strcmp(name+len-4, ".wal"))
	{
		image = R_LoadWal (name);
	}
	else if (!strcmp(name+len-4, ".tga") || !strcmp(name+len-4, ".png"))
		return R_LoadTrueColor (name, type);	// asked for by its own name (the re-release's flares)
	else
		return NULL;	// ri.Sys_Error (ERR_DROP, "R_FindImage: bad extension on: %s", name);

	if (pic)
		free(pic);
	if (palette)
		free(palette);

	return image;
}



/*
===============
R_RegisterSkin
===============
*/
struct image_s *R_RegisterSkin (char *name)
{
	return R_FindImage (name, it_skin);
}


/*
================
R_FreeUnusedImages

Any image that was not touched on this registration sequence
will be freed.
================
*/
void R_FreeUnusedImages (void)
{
	int		i;
	image_t	*image;

	for (i=0, image=r_images ; i<numr_images ; i++, image++)
	{
		if (image->registration_sequence == registration_sequence)
		{
			Com_PageInMemory ((byte *)image->pixels[0], image->width*image->height);
			continue;		// used this sequence
		}
		if (!image->registration_sequence)
			continue;		// free texture
		if (image->type == it_pic)
			continue;		// don't free pics
		// free it
		R_FreePixels (image);	// the other mip levels just follow
		memset (image, 0, sizeof(*image));
	}
}



/*
===============
R_InitImages
===============
*/
void	R_InitImages (void)
{
	registration_sequence = 1;
}

/*
===============
R_ShutdownImages
===============
*/
void	R_ShutdownImages (void)
{
	int		i;
	image_t	*image;

	for (i=0, image=r_images ; i<numr_images ; i++, image++)
	{
		if (!image->registration_sequence)
			continue;		// free texture
		// free it
		R_FreePixels (image);	// the other mip levels just follow
		memset (image, 0, sizeof(*image));
	}
}

