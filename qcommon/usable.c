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
usable.c -- whether this engine can read a file, before it tries.

The 2023 re-release's pak holds the same paths as the 1997 game's -- maps,
pictures, textures, models -- some of them in formats this engine does not
read: maps in the extended QBSP format or past the 1997 limits, 24-bit
pictures. Each loader asks here first and, given a copy it cannot read,
takes the next one down the search path (FS_LoadFileSkip), which with the
original paks underneath is id's own. The engine and the renderer both build
this file, so the server, the client and the renderer settle on the same
copy of a map, and its checksum agrees.

Each check is of the header and of the sizes the loader will trust; the
loaders still make their own.
*/

#include "../game/q_shared.h"
#include "qfiles.h"

#define IDQBSPHEADER	(('P'<<24)+('S'<<16)+('B'<<8)+'Q')

static int LL (const byte *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static float LF (const byte *p)
{
	union { int i; float f; } u;

	u.i = LL (p);
	return u.f;
}

/*
A map the collision code, the renderer and the 1997 network protocol can all
take: IBSP 38, every lump inside the file and within the limits the loaders
enforce, and a world no more than 4096 units from the origin, which is as
far as a coordinate on the wire reaches. BSPX lumps, the re-release's extra
lighting, sit after the 19 that are read and are passed over.
*/
qboolean BSP_Usable (const byte *buf, int len, char *why, int whysize)
{
	static const struct { int lump, size, max; } lumps[] = {
		{ LUMP_PLANES,		sizeof(dplane_t),		MAX_MAP_PLANES },
		{ LUMP_VERTEXES,	sizeof(dvertex_t),		MAX_MAP_VERTS },
		{ LUMP_VISIBILITY,	1,						MAX_MAP_VISIBILITY },
		{ LUMP_NODES,		sizeof(dnode_t),		MAX_MAP_NODES },
		{ LUMP_TEXINFO,		sizeof(texinfo_t),		MAX_MAP_TEXINFO },
		{ LUMP_FACES,		sizeof(dface_t),		MAX_MAP_FACES },
		{ LUMP_LIGHTING,	1,						MAX_MAP_LIGHTING },
		{ LUMP_LEAFS,		sizeof(dleaf_t),		MAX_MAP_LEAFS },
		{ LUMP_LEAFFACES,	sizeof(short),			MAX_MAP_LEAFFACES },
		{ LUMP_LEAFBRUSHES,	sizeof(short),			MAX_MAP_LEAFBRUSHES },
		{ LUMP_EDGES,		sizeof(dedge_t),		MAX_MAP_EDGES },
		{ LUMP_SURFEDGES,	sizeof(int),			MAX_MAP_SURFEDGES },
		{ LUMP_MODELS,		sizeof(dmodel_t),		MAX_MAP_MODELS },
		{ LUMP_BRUSHES,		sizeof(dbrush_t),		MAX_MAP_BRUSHES },
		{ LUMP_BRUSHSIDES,	sizeof(dbrushside_t),	MAX_MAP_BRUSHSIDES },
		{ LUMP_AREAS,		sizeof(darea_t),		MAX_MAP_AREAS },
		{ LUMP_AREAPORTALS,	sizeof(dareaportal_t),	MAX_MAP_AREAPORTALS },
		{ LUMP_ENTITIES,	1,						MAX_MAP_ENTSTRING - 1 },
	};
	int		i, ofs, size, count, ident;
	const byte	*world;

	if (len < (int)sizeof(dheader_t))
	{
		Com_sprintf (why, whysize, "is too short to be a map");
		return false;
	}
	ident = LL (buf);
	if (ident == IDQBSPHEADER)
	{
		Com_sprintf (why, whysize, "is in the re-release's extended QBSP format");
		return false;
	}
	if (ident != IDBSPHEADER || LL (buf + 4) != BSPVERSION)
	{
		Com_sprintf (why, whysize, "is not an IBSP version %i map", BSPVERSION);
		return false;
	}

	for (i = 0 ; i < (int)(sizeof(lumps) / sizeof(lumps[0])) ; i++)
	{
		ofs = LL (buf + 8 + lumps[i].lump * 8);
		size = LL (buf + 12 + lumps[i].lump * 8);
		if (ofs < 0 || size < 0 || ofs > len || size > len - ofs)
		{
			Com_sprintf (why, whysize, "has lump %i outside the file", lumps[i].lump);
			return false;
		}
		if (size % lumps[i].size)
		{
			Com_sprintf (why, whysize, "has lump %i of an odd size", lumps[i].lump);
			return false;
		}
		count = size / lumps[i].size;
		if (count > lumps[i].max)
		{
			Com_sprintf (why, whysize, "has %i in lump %i, past the limit of %i",
				count, lumps[i].lump, lumps[i].max);
			return false;
		}
	}

	if (LL (buf + 12 + LUMP_MODELS * 8) < (int)sizeof(dmodel_t))
	{
		Com_sprintf (why, whysize, "has no world model");
		return false;
	}
	world = buf + LL (buf + 8 + LUMP_MODELS * 8);
	for (i = 0 ; i < 6 ; i++)
	{
		float v = LF (world + i * 4);
		if (v < -4096 || v > 4096)
		{
			Com_sprintf (why, whysize, "reaches %g units from the origin, past the "
				"4096 a coordinate carries on the network", v);
			return false;
		}
	}
	return true;
}

/*
An .md2 within the renderer's limits, its skin up to 480 lines
(MAX_LBM_HEIGHT in ref_soft).
*/
qboolean MD2_Usable (const byte *buf, int len, char *why, int whysize)
{
	if (len < (int)sizeof(dmdl_t) || LL (buf) != IDALIASHEADER)
	{
		Com_sprintf (why, whysize, "is not an MD2 model");
		return false;
	}
	if (LL (buf + 4) != ALIAS_VERSION)
	{
		Com_sprintf (why, whysize, "is MD2 version %i, not %i", LL (buf + 4), ALIAS_VERSION);
		return false;
	}
	if (LL (buf + 12) > 480 || LL (buf + 24) > MAX_VERTS
		|| LL (buf + 32) > MAX_TRIANGLES || LL (buf + 40) > MAX_FRAMES
		|| LL (buf + 20) > MAX_MD2SKINS)
	{
		Com_sprintf (why, whysize, "is past the 1997 model limits");
		return false;
	}
	return true;
}

/*
A .pcx this engine reads: 8 bits, run-length encoded, version 5 -- every
picture the software renderer draws is in the game's palette.
*/
qboolean PCX_Usable (const byte *buf, int len, char *why, int whysize)
{
	if (len < (int)sizeof(pcx_t) || buf[0] != 0x0a || buf[1] != 5
		|| buf[2] != 1 || buf[3] != 8 || buf[65] != 1
		|| (buf[8] | (buf[9] << 8)) >= 640 || (buf[10] | (buf[11] << 8)) >= 480)
	{
		Com_sprintf (why, whysize, "is not an 8-bit paletted PCX");
		return false;
	}
	return true;
}

/*
A .wal whose four mip levels are inside it.
*/
qboolean WAL_Usable (const byte *buf, int len, char *why, int whysize)
{
	int		w, h, i, ofs, size;

	if (len < (int)sizeof(miptex_t))
	{
		Com_sprintf (why, whysize, "is too short to be a WAL texture");
		return false;
	}
	w = LL (buf + 32);
	h = LL (buf + 36);
	if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || (w & 15) || (h & 15))
	{
		Com_sprintf (why, whysize, "is not a WAL texture (%i x %i)", w, h);
		return false;
	}
	for (i = 0 ; i < MIPLEVELS ; i++)
	{
		ofs = LL (buf + 40 + i * 4);
		size = (w >> i) * (h >> i);
		if (ofs < (int)sizeof(miptex_t) || ofs > len || size > len - ofs)
		{
			Com_sprintf (why, whysize, "has a mip level outside the file");
			return false;
		}
	}
	return true;
}
