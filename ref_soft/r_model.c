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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "r_local.h"

model_t	*loadmodel;
char	loadname[32];	// for hunk tags

void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
void Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, qboolean crash);

byte	mod_novis[MAX_MAP_LEAFS/8];

#define	MAX_MOD_KNOWN	256
model_t	mod_known[MAX_MOD_KNOWN];
int		mod_numknown;

// the inline * models from the current map are kept seperate
model_t	mod_inline[MAX_MOD_KNOWN];

int		registration_sequence;
int		modfilelen;

//===============================================================================


/*
================
Mod_Modellist_f
================
*/
void Mod_Modellist_f (void)
{
	int		i;
	model_t	*mod;
	int		total;

	total = 0;
	ri.Con_Printf (PRINT_ALL,"Loaded models:\n");
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		ri.Con_Printf (PRINT_ALL, "%8i : %s\n",mod->extradatasize, mod->name);
		total += mod->extradatasize;
	}
	ri.Con_Printf (PRINT_ALL, "Total resident: %i\n", total);
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	memset (mod_novis, 0xff, sizeof(mod_novis));
}

/*
==================
Mod_Usable

Whether a copy of a model is one this renderer reads; see R_LoadUsable.
==================
*/
static qboolean Mod_Usable (const byte *buf, int len, char *why, int whysize)
{
	if (len >= 4 && LittleLong (*(unsigned *)buf) == IDALIASHEADER)
		return MD2_Usable (buf, len, why, whysize);
	if (len >= 4 && LittleLong (*(unsigned *)buf) == IDSPRITEHEADER)
		return true;
	return BSP_Usable (buf, len, why, whysize);
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, qboolean crash)
{
	model_t	*mod;
	unsigned *buf;
	int		i;
	
	if (!name[0])
		ri.Sys_Error (ERR_DROP,"Mod_ForName: NULL name");

	//
	// inline models are grabbed only from worldmodel
	//
	if (name[0] == '*')
	{
		i = atoi(name+1);
		if (i < 1 || !r_worldmodel || i >= r_worldmodel->numsubmodels)
			ri.Sys_Error (ERR_DROP, "bad inline model number");
		return &mod_inline[i];
	}

	//
	// search the currently loaded models
	//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
		if (!strcmp (mod->name, name) )
			return mod;
			
	//
	// find a free model slot spot
	//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			break;	// free spot
	}
	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			ri.Sys_Error (ERR_DROP, "mod_numknown == MAX_MOD_KNOWN");
		mod_numknown++;
	}
	strcpy (mod->name, name);
	
	//
	// load the file
	//
	modfilelen = R_LoadUsable (mod->name, (void **)&buf, Mod_Usable, true);
	if (!buf)
	{
		if (crash)
			ri.Sys_Error (ERR_DROP,"Mod_NumForName: %s not found", mod->name);
		memset (mod->name, 0, sizeof(mod->name));
		return NULL;
	}
	
	loadmodel = mod;

	//
	// fill it in
	//

	// call the apropriate loader
	
	switch (LittleLong(*(unsigned *)buf))
	{
	case IDALIASHEADER:
		loadmodel->extradata = Hunk_Begin (0x200000);
		Mod_LoadAliasModel (mod, buf);
		break;
		
	case IDSPRITEHEADER:
		loadmodel->extradata = Hunk_Begin (0x10000);
		Mod_LoadSpriteModel (mod, buf);
		break;
	
	case IDBSPHEADER:
	case IDQBSPHEADER:
		// reserved, not committed: the 2023 re-release's maps, their
		// lighting kept in colour as well and their wide faces split, take
		// more than the 16 MB of 1997 -- Call of the Machine's most of all
		loadmodel->extradata = Hunk_Begin (0x10000000);
		Mod_LoadBrushModel (mod, buf);
		break;

	default:
		ri.Sys_Error (ERR_DROP,"Mod_NumForName: unknown fileid for %s", mod->name);
		break;
	}

	loadmodel->extradatasize = Hunk_End ();

	ri.FS_FreeFile (buf);

	return mod;
}


/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;
	
	if (!model || !model->nodes)
		ri.Sys_Error (ERR_DROP, "Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents != -1)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->vis->numclusters+7)>>3;	
	out = decompressed;

#if 0
	memcpy (out, in, row);
#else
	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;		
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
#endif
	
	return decompressed;
}

/*
==============
Mod_ClusterPVS
==============
*/
byte *Mod_ClusterPVS (int cluster, model_t *model)
{
	if (cluster == -1 || !model->vis)
		return mod_novis;
	return Mod_DecompressVis ( (byte *)model->vis + model->vis->bitofs[cluster][DVIS_PVS],
		model);
}

/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;
static qboolean	mod_qbsp;		// the map is in QBSP (qfiles.h)

/*
Each face of the file as the surfaces it became (Mod_SplitSurfaces): face
i is surfaces mod_surfmap[i] up to mod_surfmap[i+1]. The nodes, leafs and
submodels number faces, and are read through it.
*/
static int		*mod_surfmap;
static int		mod_numfaces;

/*
Each leaf face is the face's pieces, in the order they are: mod_leaffacemap
says where each leaf face of the file starts among them, for the leafs.
*/
static int		*mod_leaffacemap;
static int		mod_numleaffaces;

// a QBSP node's or leaf's bound, as the short the renderer keeps
static short Mod_Short (float f)
{
	return f < -32768 ? -32768 : f > 32767 ? 32767 : (short)floor (f);
}


/*
=================
Mod_LoadLighting

Converts the 24 bit lighting down to 8 bit
by taking the brightest component
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	int		i, size;
	byte	*in;

	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		loadmodel->lightdatargb = NULL;
		return;
	}
	size = l->filelen/3;
	loadmodel->lightdata = Hunk_Alloc (size);
	in = (void *)(mod_base + l->fileofs);
	// and kept as it is, for sw_colorlight (r_light.c)
	loadmodel->lightdatargb = Hunk_Alloc (size*3);
	memcpy (loadmodel->lightdatargb, in, size*3);
	for (i=0 ; i<size ; i++, in+=3)
	{
		if (in[0] > in[1] && in[0] > in[2])
			loadmodel->lightdata[i] = in[0];
		else if (in[1] > in[0] && in[1] > in[2])
			loadmodel->lightdata[i] = in[1];
		else
			loadmodel->lightdata[i] = in[2];
	}
}


int		r_leaftovis[MAX_MAP_LEAFS];
int		r_vistoleaf[MAX_MAP_LEAFS];
int		r_numvisleafs;

void	R_NumberLeafs (mnode_t *node)
{
	mleaf_t	*leaf;
	int		leafnum;

	if (node->contents != -1)
	{
		leaf = (mleaf_t *)node;
		leafnum = leaf - loadmodel->leafs;
		if (leaf->contents & CONTENTS_SOLID)
			return;
		r_leaftovis[leafnum] = r_numvisleafs;
		r_vistoleaf[r_numvisleafs] = leafnum;
		r_numvisleafs++;
		return;
	}

	R_NumberLeafs (node->children[0]);
	R_NumberLeafs (node->children[1]);
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	int		i;

	if (!l->filelen)
	{
		loadmodel->vis = NULL;
		return;
	}
	loadmodel->vis = Hunk_Alloc ( l->filelen);	
	memcpy (loadmodel->vis, mod_base + l->fileofs, l->filelen);

	loadmodel->vis->numclusters = LittleLong (loadmodel->vis->numclusters);
	for (i=0 ; i<loadmodel->vis->numclusters ; i++)
	{
		loadmodel->vis->bitofs[i][0] = LittleLong (loadmodel->vis->bitofs[i][0]);
		loadmodel->vis->bitofs[i][1] = LittleLong (loadmodel->vis->bitofs[i][1]);
	}
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( (count+8)*sizeof(*out));		// extra for skybox

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		out->headnode = LittleLong (in->headnode);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
/*
=================
Mod_CountSharedEdges

The software renderer caches each edge once a frame and hands it to the two
faces that share it, one winding each way (R_RenderFace, cachededgeoffset):
the rule id's compiler kept, every edge in at most two faces and in opposite
directions. The 2023 re-release's compiler does not -- base1 has edges in
three and four faces, and in the same direction twice -- and a face given
another face's cached edge loses its own, and is drawn from there to the
right edge of the screen: black bars, level whatever the view's angle.
Every use of an edge past the first each way gets an edge of its own,
copied (Mod_LoadSurfedges); this counts how many, so Mod_LoadEdges can make
room. The 1997 maps have none.
=================
*/
static int	mod_extraedges;

static int Mod_CountSharedEdges (lump_t *edges, lump_t *surfedges)
{
	int		i, e, numedges, count, extra;
	int		*in;
	byte	*used;

	numedges = edges->filelen / (mod_qbsp ? sizeof(dqedge_t) : sizeof(dedge_t));
	count = surfedges->filelen / sizeof(int);
	in = (void *)(mod_base + surfedges->fileofs);
	used = calloc (numedges ? numedges : 1, 1);
	extra = 0;
	for (i=0 ; i<count ; i++)
	{
		e = LittleLong (in[i]);
		if (e >= numedges || -e >= numedges)
			continue;		// Mod_LoadFaces and the loaders say so
		if (used[e < 0 ? -e : e] & (e < 0 ? 2 : 1))
			extra++;
		else
			used[e < 0 ? -e : e] |= (e < 0 ? 2 : 1);
	}
	free (used);
	return extra;
}

void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	dqedge_t *qin;
	medge_t *out;
	int 	i, count, size;

	in = (void *)(mod_base + l->fileofs);
	qin = (void *)in;
	size = mod_qbsp ? sizeof(*qin) : sizeof(*in);
	if (l->filelen % size)
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / size;
	out = Hunk_Alloc ( (count + mod_extraedges + 13) * sizeof(*out));	// extra for skybox

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for ( i=0 ; i<count ; i++, in++, qin++, out++)
	{
		if (mod_qbsp)
		{
			out->v[0] = LittleLong(qin->v[0]);
			out->v[1] = LittleLong(qin->v[1]);
		}
		else
		{
			out->v[0] = (unsigned short)LittleShort(in->v[0]);
			out->v[1] = (unsigned short)LittleShort(in->v[1]);
		}
		if (out->v[0] >= loadmodel->numvertexes || out->v[1] >= loadmodel->numvertexes)
			ri.Sys_Error (ERR_DROP, "Mod_LoadEdges: bad vertex number in %s", loadmodel->name);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out, *step;
	int 	i, j, count;
	float	len1, len2;
	char	name[MAX_QPATH];
	int		next;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( (count+6)*sizeof(*out));	// extra for skybox

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		// Was vecs[0][j] for j up to 8, walking the first row into the
		// second. gcc diagnoses that as undefined and may drop the
		// iterations past the first row: the second texture axis of every
		// surface in the map, which is the bug Quake's model.c had too.
		for (j=0 ; j<4 ; j++)
		{
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
			out->vecs[1][j] = LittleFloat (in->vecs[1][j]);
		}
		len1 = VectorLength (out->vecs[0]);
		len2 = VectorLength (out->vecs[1]);
		len1 = (len1 + len2)/2;
		if (len1 < 0.32)
			out->mipadjust = 4;
		else if (len1 < 0.49)
			out->mipadjust = 3;
		else if (len1 < 0.99)
			out->mipadjust = 2;
		else
			out->mipadjust = 1;
#if 0
		if (len1 + len2 < 0.001)
			out->mipadjust = 1;		// don't crash
		else
			out->mipadjust = 1 / floor( (len1+len2)/2 + 0.1 );
#endif

		out->flags = LittleLong (in->flags);

		next = LittleLong (in->nexttexinfo);
		if (next > 0)
			out->next = loadmodel->texinfo + next;

		Com_sprintf (name, sizeof(name), "textures/%s.wal", in->texture);
		out->image = R_FindImage (name, it_wall);
		if (!out->image)
		{
			out->image = r_notexture_mip; // texture not found
			out->flags = 0;
		}
	}

	// count animation frames
	for (i=0 ; i<count ; i++)
	{
		out = &loadmodel->texinfo[i];
		out->numframes = 1;
		for (step = out->next ; step && step != out ; step=step->next)
			out->numframes++;
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;
	
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		
		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] + 
				v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{	
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;
		if (s->extents[i] < 16)
			s->extents[i] = 16;	// take at least one cache block
		// wider than 256 the surface cache cannot take it: such a face is
		// cut into pieces that it can, as the map loads (Mod_SplitSurfaces)
		if ( !(tex->flags & (SURF_WARP|SURF_SKY)) && s->extents[i] > 16384)
			ri.Sys_Error (ERR_DROP,"Bad surface extents");
	}
}


/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	dqface_t	*qin;
	msurface_t 	*out;
	int			i, count, surfnum, size, texinfo;
	int			planenum, side;

	in = (void *)(mod_base + l->fileofs);
	qin = (void *)in;
	size = mod_qbsp ? sizeof(*qin) : sizeof(*in);
	if (l->filelen % size)
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / size;
	out = Hunk_Alloc ( (count+6)*sizeof(*out));	// extra for skybox

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for ( surfnum=0 ; surfnum<count ; surfnum++, in++, qin++, out++)
	{
		if (mod_qbsp)
		{
			out->firstedge = LittleLong(qin->firstedge);
			out->numedges = LittleLong(qin->numedges);
			planenum = LittleLong(qin->planenum);
			side = LittleLong(qin->side);
			texinfo = LittleLong(qin->texinfo);
		}
		else
		{
			out->firstedge = LittleLong(in->firstedge);
			out->numedges = LittleShort(in->numedges);
			planenum = (unsigned short)LittleShort(in->planenum);
			side = LittleShort(in->side);
			texinfo = LittleShort(in->texinfo);
		}
		if (out->numedges < 3)
			ri.Sys_Error (ERR_DROP,"Surface with %i edges", out->numedges);
		if (out->firstedge < 0 || out->firstedge > loadmodel->numsurfedges - out->numedges
			|| planenum < 0 || planenum >= loadmodel->numplanes
			|| texinfo < 0 || texinfo >= loadmodel->numtexinfo)
			ri.Sys_Error (ERR_DROP, "Mod_LoadFaces: bad face in %s", loadmodel->name);
		out->flags = 0;

		if (side)
			out->flags |= SURF_PLANEBACK;			

		out->plane = loadmodel->planes + planenum;

		out->texinfo = loadmodel->texinfo + texinfo;

		CalcSurfaceExtents (out);
				
	// lighting info is converted from 24 bit on disk to 8 bit

		for (i=0 ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = mod_qbsp ? qin->styles[i] : in->styles[i];
		i = LittleLong(mod_qbsp ? qin->lightofs : in->lightofs);
		if (i == -1)
		{
			out->samples = NULL;
			out->samplesrgb = NULL;
		}
		else
		{
			out->samples = loadmodel->lightdata + i/3;
			out->samplesrgb = loadmodel->lightdatargb + i/3*3;
		}
		
	// set the drawing flags flag
		
		if (!out->texinfo->image)
			continue;
		if (out->texinfo->flags & SURF_SKY)
		{
			out->flags |= SURF_DRAWSKY;
			continue;
		}
		
		if (out->texinfo->flags & SURF_WARP)
		{
			out->flags |= SURF_DRAWTURB;
			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			continue;
		}
//==============
//PGM
		// this marks flowing surfaces as turbulent, but with the new
		// SURF_FLOW flag.
		if (out->texinfo->flags & SURF_FLOWING)
		{
			out->flags |= SURF_DRAWTURB | SURF_FLOW;
			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			continue;
		}
//PGM
//==============
	}
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents != -1)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p, size, first, num;
	dnode_t		*in;
	dqnode_t	*qin;
	mnode_t 	*out;

	in = (void *)(mod_base + l->fileofs);
	qin = (void *)in;
	size = mod_qbsp ? sizeof(*qin) : sizeof(*in);
	if (l->filelen % size)
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / size;
	out = Hunk_Alloc ( count*sizeof(*out));	

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, qin++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = mod_qbsp ? Mod_Short (LittleFloat (qin->mins[j])) : LittleShort (in->mins[j]);
			out->minmaxs[3+j] = mod_qbsp ? Mod_Short (LittleFloat (qin->maxs[j])) : LittleShort (in->maxs[j]);
		}
	
		p = LittleLong(mod_qbsp ? qin->planenum : in->planenum);
		if (p < 0 || p >= loadmodel->numplanes)
			ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: bad plane in %s", loadmodel->name);
		out->plane = loadmodel->planes + p;

		first = mod_qbsp ? (int)LittleLong (qin->firstface) : (unsigned short)LittleShort (in->firstface);
		num = mod_qbsp ? (int)LittleLong (qin->numfaces) : (unsigned short)LittleShort (in->numfaces);
		if (first < 0 || num < 0 || first > mod_numfaces - num)
			ri.Sys_Error (ERR_DROP, "Mod_LoadNodes: bad faces in %s", loadmodel->name);
		out->firstsurface = mod_surfmap[first];
		out->numsurfaces = mod_surfmap[first + num] - mod_surfmap[first];
		out->contents = CONTENTS_NODE;	// differentiate from leafs
		
		for (j=0 ; j<2 ; j++)
		{
			p = LittleLong (mod_qbsp ? qin->children[j] : in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
		}
	}
	
	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	dqleaf_t	*qin;
	mleaf_t 	*out;
	int			i, j, count, size, first, num;

	in = (void *)(mod_base + l->fileofs);
	qin = (void *)in;
	size = mod_qbsp ? sizeof(*qin) : sizeof(*in);
	if (l->filelen % size)
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / size;
	out = Hunk_Alloc ( count*sizeof(*out));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, qin++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = mod_qbsp ? Mod_Short (LittleFloat (qin->mins[j])) : LittleShort (in->mins[j]);
			out->minmaxs[3+j] = mod_qbsp ? Mod_Short (LittleFloat (qin->maxs[j])) : LittleShort (in->maxs[j]);
		}

		if (mod_qbsp)
		{
			out->contents = LittleLong(qin->contents);
			out->cluster = LittleLong(qin->cluster);
			out->area = LittleLong(qin->area);
			first = LittleLong(qin->firstleafface);
			num = LittleLong(qin->numleaffaces);
		}
		else
		{
			out->contents = LittleLong(in->contents);
			out->cluster = LittleShort(in->cluster);
			out->area = LittleShort(in->area);
			first = (unsigned short)LittleShort(in->firstleafface);
			num = (unsigned short)LittleShort(in->numleaffaces);
		}
		// the leaf faces as they are once each face is its pieces
		if (first < 0 || num < 0 || first > mod_numleaffaces - num)
			ri.Sys_Error (ERR_DROP, "Mod_LoadLeafs: bad leaf faces in %s", loadmodel->name);
		out->firstmarksurface = loadmodel->marksurfaces + mod_leaffacemap[first];
		out->nummarksurfaces = mod_leaffacemap[first + num] - mod_leaffacemap[first];
	}	
}


/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{	
	int		i, j, k, count, total, size;
	unsigned short		*in;
	unsigned	*qin;
	msurface_t **out;
	
	in = (void *)(mod_base + l->fileofs);
	qin = (void *)in;
	size = mod_qbsp ? sizeof(*qin) : sizeof(*in);
	if (l->filelen % size)
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / size;
	mod_numleaffaces = count;
	mod_leaffacemap = malloc ((count + 1) * sizeof(int));

	total = 0;
	for ( i=0 ; i<count ; i++)
	{
		j = mod_qbsp ? (int)LittleLong(qin[i]) : (unsigned short)LittleShort(in[i]);
		if (j < 0 || j >= mod_numfaces)
			ri.Sys_Error (ERR_DROP,"Mod_ParseMarksurfaces: bad surface number");
		mod_leaffacemap[i] = total;
		total += mod_surfmap[j+1] - mod_surfmap[j];
	}
	mod_leaffacemap[count] = total;

	out = Hunk_Alloc ( (total ? total : 1)*sizeof(*out));	
	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = total;

	for ( i=0 ; i<count ; i++)
	{
		j = mod_qbsp ? (int)LittleLong(qin[i]) : (unsigned short)LittleShort(in[i]);
		for (k=mod_surfmap[j] ; k<mod_surfmap[j+1] ; k++)
			*out++ = loadmodel->surfaces + k;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{	
	int		i, count;
	int		*in, *out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( (count+24)*sizeof(*out));	// extra for skybox

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for ( i=0 ; i<count ; i++)
	{
		out[i] = LittleLong (in[i]);
		if (out[i] >= loadmodel->numedges || -out[i] >= loadmodel->numedges)
			ri.Sys_Error (ERR_DROP, "Mod_LoadSurfedges: bad edge number in %s", loadmodel->name);
	}

	// see Mod_CountSharedEdges
	if (mod_extraedges)
	{
		int		e, n, original = loadmodel->numedges;
		byte	*used = calloc (original ? original : 1, 1);

		for (i=0 ; i<count ; i++)
		{
			e = out[i];
			if (e >= original || -e >= original)
				continue;
			n = e < 0 ? -e : e;
			if (!(used[n] & (e < 0 ? 2 : 1)))
			{
				used[n] |= (e < 0 ? 2 : 1);
				continue;
			}
			loadmodel->edges[loadmodel->numedges] = loadmodel->edges[n];
			loadmodel->edges[loadmodel->numedges].cachededgeoffset = 0;
			out[i] = e < 0 ? -loadmodel->numedges : loadmodel->numedges;
			loadmodel->numedges++;
		}
		free (used);
		ri.Con_Printf (PRINT_DEVELOPER, "%s: %i edges shared past two faces, copied\n",
			loadmodel->name, mod_extraedges);
	}
}

/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Sys_Error (ERR_DROP,"MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc ( (count+6)*sizeof(*out));		// extra for skybox
	
	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}


/*
=================
Mod_LoadDecoupledLightmaps

The 2023 re-release's maps leave every face's lightofs at -1 and keep their
lighting in a BSPX lump, DECOUPLED_LM: for each face a lightmap of its own
size, anywhere in the lighting lump, with a matrix from world space to its
texels. This renderer lights a surface from a grid of samples 16 texture
units apart, starting at its texturemins, which is what the 1997 compiler
wrote and what r_light.c and the surface cache index. So the re-release's
lightmaps are resampled onto that grid here, once, as the map loads: each
grid point is taken back to the face's plane in world space, then into the
re-release lightmap, and read bilinearly. Each RGB sample becomes one
brightness as Mod_LoadLighting makes it, the brightest of the three, and is
kept in colour as well, for sw_colorlight.
=================
*/
typedef struct
{
	unsigned short	width, height;
	int				offset;			// into the lighting lump, -1 for none
	float			vecs[2][4];		// world to lightmap texels
} dlmface_t;

static void Mod_LoadDecoupledLightmaps (dheader_t *header, int filelen)
{
	int			i, j, k, n, ofs, end, numlumps, lofs = 0, llen = 0;
	int			smax, tmax, styles, total, w, h, style;
	byte		*e, *light, *rgb, *out, *outrgb;
	dlmface_t	*lm;
	msurface_t	*surf;
	float		*a, *b, *c, det, bc[3], ca[3], ab[3], r0, r1, r2;
	float		p0[3], ds[3], dt[3], p[3], u, v, fu, fv;
	int			iu, iv, iu1, iv1, x;
	lump_t		*ll = &header->lumps[LUMP_LIGHTING];

	end = 0;
	for (i=0 ; i<HEADER_LUMPS ; i++)
		if (header->lumps[i].fileofs + header->lumps[i].filelen > end)
			end = header->lumps[i].fileofs + header->lumps[i].filelen;
	ofs = (end + 3) & ~3;
	if (ofs + 8 > filelen || memcmp (mod_base + ofs, "BSPX", 4))
		return;
	numlumps = LittleLong (*(int *)(mod_base + ofs + 4));
	for (i=0 ; i<numlumps && ofs + 8 + (i+1)*32 <= filelen ; i++)
	{
		e = mod_base + ofs + 8 + i*32;
		if (!strncmp ((char *)e, "DECOUPLED_LM", 24))
		{
			lofs = LittleLong (*(int *)(e + 24));
			llen = LittleLong (*(int *)(e + 28));
		}
	}
	if (!llen)
		return;
	if (lofs < 0 || llen < 0 || lofs > filelen || llen > filelen - lofs
		|| llen != loadmodel->numsurfaces * (int)sizeof(dlmface_t))
	{
		ri.Con_Printf (PRINT_ALL, "%s: DECOUPLED_LM is not one entry a face; unlit\n", loadmodel->name);
		return;
	}
	lm = (dlmface_t *)(mod_base + lofs);
	light = mod_base + ll->fileofs;

	// how much the 16-unit grids take
	total = 0;
	for (i=0, surf = loadmodel->surfaces ; i<loadmodel->numsurfaces ; i++, surf++)
	{
		if (LittleLong (lm[i].offset) < 0 || (surf->flags & (SURF_DRAWTURB|SURF_DRAWSKY)))
			continue;
		for (styles=0 ; styles<MAXLIGHTMAPS && surf->styles[styles] != 255 ; styles++)
			;
		total += ((surf->extents[0]>>4)+1) * ((surf->extents[1]>>4)+1) * styles;
	}
	if (!total)
		return;
	out = Hunk_Alloc (total);
	outrgb = Hunk_Alloc (total*3);

	for (i=0, surf = loadmodel->surfaces ; i<loadmodel->numsurfaces ; i++, surf++)
	{
		ofs = LittleLong (lm[i].offset);
		w = LittleShort (lm[i].width);
		h = LittleShort (lm[i].height);
		if (ofs < 0 || (surf->flags & (SURF_DRAWTURB|SURF_DRAWSKY)) || w <= 0 || h <= 0)
			continue;
		for (styles=0 ; styles<MAXLIGHTMAPS && surf->styles[styles] != 255 ; styles++)
			;
		if (!styles || ofs + w*h*3*styles > ll->filelen)
			continue;

		// the point on the face's plane at texture coordinates (s, t):
		// three planes, s, t and the face's own, solved by Cramer's rule
		a = surf->texinfo->vecs[0];
		b = surf->texinfo->vecs[1];
		c = surf->plane->normal;
		CrossProduct (b, c, bc);
		CrossProduct (c, a, ca);
		CrossProduct (a, b, ab);
		det = DotProduct (a, bc);
		if (fabs (det) < 1e-6)
			continue;
		r0 = surf->texturemins[0] - a[3];
		r1 = surf->texturemins[1] - b[3];
		r2 = surf->plane->dist;
		for (k=0 ; k<3 ; k++)
		{
			p0[k] = (r0*bc[k] + r1*ca[k] + r2*ab[k]) / det;
			ds[k] = 16 * bc[k] / det;
			dt[k] = 16 * ca[k] / det;
		}

		smax = (surf->extents[0]>>4)+1;
		tmax = (surf->extents[1]>>4)+1;
		surf->samples = out;
		surf->samplesrgb = outrgb;
		for (style=0 ; style<styles ; style++)
		{
			rgb = light + ofs + style*w*h*3;
			for (n=0 ; n<tmax ; n++)
				for (j=0 ; j<smax ; j++)
				{
					for (k=0 ; k<3 ; k++)
						p[k] = p0[k] + j*ds[k] + n*dt[k];
					// texel centres are half a texel in
					u = DotProduct (p, lm[i].vecs[0]) + lm[i].vecs[0][3] - 0.5f;
					v = DotProduct (p, lm[i].vecs[1]) + lm[i].vecs[1][3] - 0.5f;
					if (u < 0) u = 0;
					if (v < 0) v = 0;
					if (u > w-1) u = w-1;
					if (v > h-1) v = h-1;
					iu = (int)u;  iv = (int)v;
					fu = u - iu;  fv = v - iv;
					iu1 = iu+1 < w ? iu+1 : iu;
					iv1 = iv+1 < h ? iv+1 : iv;
#define LUM(x,y) (rgb[((y)*w+(x))*3] > rgb[((y)*w+(x))*3+1] \
	? (rgb[((y)*w+(x))*3] > rgb[((y)*w+(x))*3+2] ? rgb[((y)*w+(x))*3] : rgb[((y)*w+(x))*3+2]) \
	: (rgb[((y)*w+(x))*3+1] > rgb[((y)*w+(x))*3+2] ? rgb[((y)*w+(x))*3+1] : rgb[((y)*w+(x))*3+2]))
					x = (int)(LUM(iu,iv)*(1-fu)*(1-fv) + LUM(iu1,iv)*fu*(1-fv)
						+ LUM(iu,iv1)*(1-fu)*fv + LUM(iu1,iv1)*fu*fv + 0.5f);
#undef LUM
					*out++ = x > 255 ? 255 : x;
#define CH(x,y) rgb[((y)*w+(x))*3+k]
					for (k=0 ; k<3 ; k++)
					{
						x = (int)(CH(iu,iv)*(1-fu)*(1-fv) + CH(iu1,iv)*fu*(1-fv)
							+ CH(iu,iv1)*(1-fu)*fv + CH(iu1,iv1)*fu*fv + 0.5f);
						*outrgb++ = x > 255 ? 255 : x;
					}
#undef CH
				}
		}
	}
	ri.Con_Printf (PRINT_DEVELOPER, "%s: lighting resampled from DECOUPLED_LM\n", loadmodel->name);
}

/*
=================
Mod_MarkColoredSurfaces

Which surfaces' lightmaps have colour in them, for r_light.c to light those
in colour and to leave the others to the colormap alone, as they were. A
sample is coloured when its dimmest channel is under seven-eighths of its
brightest; the faint casts of the 1997 compiler's bounced light are not.
=================
*/
static void Mod_MarkColoredSurfaces (void)
{
	int			i, j, n, lo, hi;
	msurface_t	*surf;
	byte		*p;

	for (i=0, surf = loadmodel->surfaces ; i<loadmodel->numsurfaces ; i++, surf++)
	{
		surf->colored = false;
		if (!surf->samplesrgb)
			continue;
		for (n=0 ; n<MAXLIGHTMAPS && surf->styles[n] != 255 ; n++)
			;
		n *= ((surf->extents[0]>>4)+1) * ((surf->extents[1]>>4)+1);
		for (j=0, p = surf->samplesrgb ; j<n ; j++, p+=3)
		{
			hi = p[0] > p[1] ? p[0] : p[1];
			if (p[2] > hi) hi = p[2];
			lo = p[0] < p[1] ? p[0] : p[1];
			if (p[2] < lo) lo = p[2];
			if (hi >= 8 && lo*8 < hi*7)
			{
				surf->colored = true;
				break;
			}
		}
	}
}


/*
=================
Mod_SplitSurfaces

The software renderer's surface cache takes a face no more than 256 units
across its texture, and id's compiler cut faces to that size; the 2023
re-release's compiler did not, for the Quake 64 maps, Call of the
Machine's and a few others -- faces up to 3264 units across. Each such
face is cut here, as the map loads, along its texture's axes every 240
units -- 240 so that a piece's 16-unit lightmap grid, rounded out each
way, is still within 256 -- into pieces the cache takes. The pieces keep
the face's plane, texture and flags; each has new vertices and edges of
its own, and its part of the face's lightmap, which is on the same 16-unit
grid and so is copied, not resampled. A point where a cut crosses an edge
is worked out from the edge's two ends in the same order whichever piece
asks, so pieces meet exactly.

The nodes, leafs and submodels number the file's faces: mod_surfmap says
which surfaces each became.
=================
*/
#define SPLIT_SIZE		240
#define MAX_SPLIT_VERTS	64

typedef struct
{
	int		parent;
	int		numverts;
	vec3_t	verts[MAX_SPLIT_VERTS];
} splitpiece_t;

static float Mod_TexCoord (vec3_t v, float *vec)
{
	return v[0]*vec[0] + v[1]*vec[1] + v[2]*vec[2] + vec[3];
}

// the part of in where the coordinate along vec is >= c (ge) or <= c
static int Mod_ClipPoly (vec3_t *in, int n, float *vec, float c, qboolean ge, vec3_t *out)
{
	int		i, j, k, m = 0;
	float	di, dj, f;
	float	*a, *b;
	qboolean	ini, inj;

	for (i=0 ; i<n ; i++)
	{
		j = (i + 1) % n;
		di = Mod_TexCoord (in[i], vec) - c;
		dj = Mod_TexCoord (in[j], vec) - c;
		ini = ge ? di >= 0 : di <= 0;
		inj = ge ? dj >= 0 : dj <= 0;
		if (ini && m < MAX_SPLIT_VERTS)
		{
			VectorCopy (in[i], out[m]);		// a macro: m++ in it is three
			m++;
		}
		if (ini != inj && di != 0 && dj != 0 && m < MAX_SPLIT_VERTS)
		{
			// from the end that sorts first, so both sides get one point
			a = in[i]; b = in[j];
			if (a[0] > b[0] || (a[0] == b[0] && (a[1] > b[1] || (a[1] == b[1] && a[2] > b[2]))))
			{
				a = in[j]; b = in[i];
				f = dj; dj = di; di = f;
			}
			f = di / (di - dj);
			for (k=0 ; k<3 ; k++)
				out[m][k] = a[k] + f * (b[k] - a[k]);
			m++;
		}
	}
	return m;
}

static qboolean Mod_NeedsSplit (msurface_t *surf)
{
	return !(surf->flags & (SURF_DRAWTURB|SURF_DRAWSKY))
		&& (surf->extents[0] > 256 || surf->extents[1] > 256);
}

static void Mod_SplitSurfaces (void)
{
	msurface_t	*oldsurfs = loadmodel->surfaces, *surf, *piece, *newsurfs;
	mvertex_t	*newverts;
	medge_t		*newedges;
	int			*newsurfedges;
	splitpiece_t	*pieces = NULL;
	int			numpieces = 0, maxpieces = 0, split = 0, newpolyverts = 0;
	int			i, j, k, e, n, n2, si, ti, style, styles, total;
	int			smax, tmax, psmax, ptmax, ds, dt, row;
	float		mins[2], maxs[2], val;
	vec3_t		poly[MAX_SPLIT_VERTS], strip[MAX_SPLIT_VERTS], tmp[MAX_SPLIT_VERTS];
	float		*vec;
	int			numorig = loadmodel->numsurfaces;

	mod_numfaces = numorig;
	mod_surfmap = malloc ((numorig + 1) * sizeof(int));

	// the pieces of each face that needs cutting
	for (i=0 ; i<numorig ; i++)
	{
		surf = &oldsurfs[i];
		if (!Mod_NeedsSplit (surf))
			continue;
		n = surf->numedges < MAX_SPLIT_VERTS ? surf->numedges : MAX_SPLIT_VERTS;
		mins[0] = mins[1] = 999999;
		maxs[0] = maxs[1] = -999999;
		for (j=0 ; j<n ; j++)
		{
			e = loadmodel->surfedges[surf->firstedge + j];
			VectorCopy (loadmodel->vertexes[e >= 0 ? loadmodel->edges[e].v[0]
				: loadmodel->edges[-e].v[1]].position, poly[j]);
			for (k=0 ; k<2 ; k++)
			{
				val = Mod_TexCoord (poly[j], surf->texinfo->vecs[k]);
				if (val < mins[k]) mins[k] = val;
				if (val > maxs[k]) maxs[k] = val;
			}
		}
		split++;
		for (si = (int)floor (mins[0] / SPLIT_SIZE) ; si * SPLIT_SIZE < maxs[0] ; si++)
		{
			vec = surf->texinfo->vecs[0];
			n2 = Mod_ClipPoly (poly, n, vec, si * SPLIT_SIZE, true, tmp);
			n2 = Mod_ClipPoly (tmp, n2, vec, (si+1) * SPLIT_SIZE, false, strip);
			if (n2 < 3)
				continue;
			for (ti = (int)floor (mins[1] / SPLIT_SIZE) ; ti * SPLIT_SIZE < maxs[1] ; ti++)
			{
				splitpiece_t	*sp;
				int				m;

				vec = surf->texinfo->vecs[1];
				m = Mod_ClipPoly (strip, n2, vec, ti * SPLIT_SIZE, true, tmp);
				if (numpieces == maxpieces)
				{
					maxpieces = maxpieces ? maxpieces * 2 : 256;
					pieces = realloc (pieces, maxpieces * sizeof(*pieces));
				}
				sp = &pieces[numpieces];
				m = Mod_ClipPoly (tmp, m, vec, (ti+1) * SPLIT_SIZE, false, sp->verts);
				if (m < 3)
					continue;
				sp->parent = i;
				sp->numverts = m;
				newpolyverts += m;
				numpieces++;
			}
		}
	}

	if (!split)
	{
		for (i=0 ; i<=numorig ; i++)
			mod_surfmap[i] = i;
		return;
	}

	// room for the pieces, and for the sky's six faces after them
	total = numorig - split + numpieces;
	newsurfs = Hunk_Alloc ((total + 6) * sizeof(msurface_t));
	newverts = Hunk_Alloc ((loadmodel->numvertexes + newpolyverts + 8) * sizeof(mvertex_t));
	newedges = Hunk_Alloc ((loadmodel->numedges + newpolyverts + 13) * sizeof(medge_t));
	newsurfedges = Hunk_Alloc ((loadmodel->numsurfedges + newpolyverts + 24) * sizeof(int));
	memcpy (newverts, loadmodel->vertexes, loadmodel->numvertexes * sizeof(mvertex_t));
	memcpy (newedges, loadmodel->edges, loadmodel->numedges * sizeof(medge_t));
	memcpy (newsurfedges, loadmodel->surfedges, loadmodel->numsurfedges * sizeof(int));
	loadmodel->vertexes = newverts;
	loadmodel->edges = newedges;
	loadmodel->surfedges = newsurfedges;

	for (i=0, j=0, k=0 ; i<numorig ; i++)
	{
		mod_surfmap[i] = k;
		if (!Mod_NeedsSplit (&oldsurfs[i]))
		{
			newsurfs[k++] = oldsurfs[i];
			continue;
		}
		surf = &oldsurfs[i];
		for (styles=0 ; styles<MAXLIGHTMAPS && surf->styles[styles] != 255 ; styles++)
			;
		smax = (surf->extents[0]>>4)+1;
		tmax = (surf->extents[1]>>4)+1;
		for ( ; j<numpieces && pieces[j].parent == i ; j++)
		{
			splitpiece_t	*sp = &pieces[j];
			int				v0 = loadmodel->numvertexes, e0 = loadmodel->numedges;

			piece = &newsurfs[k++];
			*piece = *surf;
			piece->firstedge = loadmodel->numsurfedges;
			piece->numedges = sp->numverts;
			for (e=0 ; e<sp->numverts ; e++)
			{
				VectorCopy (sp->verts[e], newverts[v0 + e].position);
				newedges[e0 + e].v[0] = v0 + e;
				newedges[e0 + e].v[1] = v0 + (e + 1) % sp->numverts;
				newedges[e0 + e].cachededgeoffset = 0;
				newsurfedges[loadmodel->numsurfedges + e] = e0 + e;
			}
			loadmodel->numvertexes += sp->numverts;
			loadmodel->numedges += sp->numverts;
			loadmodel->numsurfedges += sp->numverts;
			CalcSurfaceExtents (piece);

			// its part of the face's lightmap, on the same grid
			piece->samples = piece->samplesrgb = NULL;
			if (!surf->samples || !styles)
				continue;
			psmax = (piece->extents[0]>>4)+1;
			ptmax = (piece->extents[1]>>4)+1;
			ds = (piece->texturemins[0] - surf->texturemins[0]) >> 4;
			dt = (piece->texturemins[1] - surf->texturemins[1]) >> 4;
			if (ds < 0 || dt < 0 || ds + psmax > smax || dt + ptmax > tmax)
				continue;		// cannot happen: a piece is inside its face
			piece->samples = Hunk_Alloc (psmax * ptmax * styles);
			if (surf->samplesrgb)
				piece->samplesrgb = Hunk_Alloc (psmax * ptmax * styles * 3);
			for (style=0 ; style<styles ; style++)
				for (row=0 ; row<ptmax ; row++)
				{
					memcpy (piece->samples + (style*ptmax + row)*psmax,
						surf->samples + (style*tmax + dt + row)*smax + ds, psmax);
					if (surf->samplesrgb)
						memcpy (piece->samplesrgb + ((style*ptmax + row)*psmax)*3,
							surf->samplesrgb + ((style*tmax + dt + row)*smax + ds)*3, psmax*3);
				}
		}
	}
	mod_surfmap[numorig] = k;
	loadmodel->surfaces = newsurfs;
	loadmodel->numsurfaces = k;
	free (pieces);

	ri.Con_Printf (PRINT_DEVELOPER, "%s: %i faces wider than the surface cache takes, cut into %i\n",
		loadmodel->name, split, numpieces);
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	int			i;
	dheader_t	*header;
	dmodel_t 	*bm;
	
	loadmodel->type = mod_brush;
	if (loadmodel != mod_known)
		ri.Sys_Error (ERR_DROP, "Loaded a brush model after the world");
	
	header = (dheader_t *)buffer;

	i = LittleLong (header->version);
	if (i != BSPVERSION)
		ri.Sys_Error (ERR_DROP,"Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, i, BSPVERSION);
	mod_qbsp = LittleLong (header->ident) == IDQBSPHEADER;

// swap all the lumps
	mod_base = (byte *)header;

	for (i=0 ; i<sizeof(dheader_t)/4 ; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap
	
	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	mod_extraedges = Mod_CountSharedEdges (&header->lumps[LUMP_EDGES], &header->lumps[LUMP_SURFEDGES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES]);
	Mod_LoadDecoupledLightmaps (header, modfilelen);
	Mod_SplitSurfaces ();
	Mod_MarkColoredSurfaces ();
	Mod_LoadMarksurfaces (&header->lumps[LUMP_LEAFFACES]);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes (&header->lumps[LUMP_NODES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);
	r_numvisleafs = 0;
	R_NumberLeafs (loadmodel->nodes);
	
//
// set up the submodels
//
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		model_t	*starmod;

		bm = &mod->submodels[i];
		starmod = &mod_inline[i];

		*starmod = *loadmodel;
		
		if (bm->firstface < 0 || bm->numfaces < 0 || bm->firstface > mod_numfaces - bm->numfaces)
			ri.Sys_Error (ERR_DROP, "Inline model %i has bad faces", i);
		starmod->firstmodelsurface = mod_surfmap[bm->firstface];
		starmod->nummodelsurfaces = mod_surfmap[bm->firstface + bm->numfaces] - mod_surfmap[bm->firstface];
		starmod->firstnode = bm->headnode;
		if (starmod->firstnode >= loadmodel->numnodes)
			ri.Sys_Error (ERR_DROP, "Inline model %i has bad firstnode", i);

		VectorCopy (bm->maxs, starmod->maxs);
		VectorCopy (bm->mins, starmod->mins);
	
		if (i == 0)
			*loadmodel = *starmod;
	}

	free (mod_surfmap);
	mod_surfmap = NULL;
	free (mod_leaffacemap);
	mod_leaffacemap = NULL;

	R_InitSkyBox ();
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int					i, j;
	dmdl_t				*pinmodel, *pheader;
	dstvert_t			*pinst, *poutst;
	dtriangle_t			*pintri, *pouttri;
	daliasframe_t		*pinframe, *poutframe;
	int					*pincmd, *poutcmd;
	int					version;

	pinmodel = (dmdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		ri.Sys_Error (ERR_DROP, "%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

	pheader = Hunk_Alloc (LittleLong(pinmodel->ofs_end));
	
	// byte swap the header fields and sanity check
	for (i=0 ; i<sizeof(dmdl_t)/4 ; i++)
		((int *)pheader)[i] = LittleLong (((int *)buffer)[i]);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		ri.Sys_Error (ERR_DROP, "model %s has a skin taller than %d", mod->name,
				   MAX_LBM_HEIGHT);

	if (pheader->num_xyz <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no vertices", mod->name);

	if (pheader->num_xyz > MAX_VERTS)
		ri.Sys_Error (ERR_DROP, "model %s has too many vertices", mod->name);

	if (pheader->num_st <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no st vertices", mod->name);

	if (pheader->num_tris <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no triangles", mod->name);

	if (pheader->num_frames <= 0)
		ri.Sys_Error (ERR_DROP, "model %s has no frames", mod->name);

//
// load base s and t vertices (not used in gl version)
//
	pinst = (dstvert_t *) ((byte *)pinmodel + pheader->ofs_st);
	poutst = (dstvert_t *) ((byte *)pheader + pheader->ofs_st);

	for (i=0 ; i<pheader->num_st ; i++)
	{
		poutst[i].s = LittleShort (pinst[i].s);
		poutst[i].t = LittleShort (pinst[i].t);
	}

//
// load triangle lists
//
	pintri = (dtriangle_t *) ((byte *)pinmodel + pheader->ofs_tris);
	pouttri = (dtriangle_t *) ((byte *)pheader + pheader->ofs_tris);

	for (i=0 ; i<pheader->num_tris ; i++)
	{
		for (j=0 ; j<3 ; j++)
		{
			pouttri[i].index_xyz[j] = LittleShort (pintri[i].index_xyz[j]);
			pouttri[i].index_st[j] = LittleShort (pintri[i].index_st[j]);
		}
	}

//
// load the frames
//
	for (i=0 ; i<pheader->num_frames ; i++)
	{
		pinframe = (daliasframe_t *) ((byte *)pinmodel 
			+ pheader->ofs_frames + i * pheader->framesize);
		poutframe = (daliasframe_t *) ((byte *)pheader 
			+ pheader->ofs_frames + i * pheader->framesize);

		memcpy (poutframe->name, pinframe->name, sizeof(poutframe->name));
		for (j=0 ; j<3 ; j++)
		{
			poutframe->scale[j] = LittleFloat (pinframe->scale[j]);
			poutframe->translate[j] = LittleFloat (pinframe->translate[j]);
		}
		// verts are all 8 bit, so no swapping needed
		memcpy (poutframe->verts, pinframe->verts, 
			pheader->num_xyz*sizeof(dtrivertx_t));

	}

	mod->type = mod_alias;

	//
	// load the glcmds
	//
	pincmd = (int *) ((byte *)pinmodel + pheader->ofs_glcmds);
	poutcmd = (int *) ((byte *)pheader + pheader->ofs_glcmds);
	for (i=0 ; i<pheader->num_glcmds ; i++)
		poutcmd[i] = LittleLong (pincmd[i]);


	// register all skins
	memcpy ((char *)pheader + pheader->ofs_skins, (char *)pinmodel + pheader->ofs_skins,
		pheader->num_skins*MAX_SKINNAME);
	for (i=0 ; i<pheader->num_skins ; i++)
	{
		mod->skins[i] = R_FindImage ((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME, it_skin);
	}
}

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/

/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	dsprite_t	*sprin, *sprout;
	int			i;

	sprin = (dsprite_t *)buffer;
	sprout = Hunk_Alloc (modfilelen);

	sprout->ident = LittleLong (sprin->ident);
	sprout->version = LittleLong (sprin->version);
	sprout->numframes = LittleLong (sprin->numframes);

	if (sprout->version != SPRITE_VERSION)
		ri.Sys_Error (ERR_DROP, "%s has wrong version number (%i should be %i)",
				 mod->name, sprout->version, SPRITE_VERSION);

	if (sprout->numframes > MAX_MD2SKINS)
		ri.Sys_Error (ERR_DROP, "%s has too many frames (%i > %i)",
				 mod->name, sprout->numframes, MAX_MD2SKINS);

	// byte swap everything
	for (i=0 ; i<sprout->numframes ; i++)
	{
		sprout->frames[i].width = LittleLong (sprin->frames[i].width);
		sprout->frames[i].height = LittleLong (sprin->frames[i].height);
		sprout->frames[i].origin_x = LittleLong (sprin->frames[i].origin_x);
		sprout->frames[i].origin_y = LittleLong (sprin->frames[i].origin_y);
		memcpy (sprout->frames[i].name, sprin->frames[i].name, MAX_SKINNAME);
		mod->skins[i] = R_FindImage (sprout->frames[i].name, it_sprite);
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginRegistration

Specifies the model that will be used as the world
@@@@@@@@@@@@@@@@@@@@@
*/
void R_BeginRegistration (char *model)
{
	char	fullname[MAX_QPATH];
	cvar_t	*flushmap;

	registration_sequence++;
	r_oldviewcluster = -1;		// force markleafs
	Com_sprintf (fullname, sizeof(fullname), "maps/%s.bsp", model);

	D_FlushCaches ();
	// explicitly free the old map if different
	// this guarantees that mod_known[0] is the world map
	flushmap = ri.Cvar_Get ("flushmap", "0", 0);
	if ( strcmp(mod_known[0].name, fullname) || flushmap->value)
		Mod_Free (&mod_known[0]);
	r_worldmodel = R_RegisterModel (fullname);
	R_NewMap ();
}


/*
@@@@@@@@@@@@@@@@@@@@@
R_RegisterModel

@@@@@@@@@@@@@@@@@@@@@
*/
struct model_s *R_RegisterModel (char *name)
{
	model_t	*mod;
	int		i;
	dsprite_t	*sprout;
	dmdl_t		*pheader;

	mod = Mod_ForName (name, false);
	if (mod)
	{
		mod->registration_sequence = registration_sequence;

		// register any images used by the models
		if (mod->type == mod_sprite)
		{
			sprout = (dsprite_t *)mod->extradata;
			for (i=0 ; i<sprout->numframes ; i++)
				mod->skins[i] = R_FindImage (sprout->frames[i].name, it_sprite);
		}
		else if (mod->type == mod_alias)
		{
			pheader = (dmdl_t *)mod->extradata;
			for (i=0 ; i<pheader->num_skins ; i++)
				mod->skins[i] = R_FindImage ((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME, it_skin);
//PGM
			mod->numframes = pheader->num_frames;
//PGM
		}
		else if (mod->type == mod_brush)
		{
			for (i=0 ; i<mod->numtexinfo ; i++)
				mod->texinfo[i].image->registration_sequence = registration_sequence;
		}
	}
	return mod;
}

/*
@@@@@@@@@@@@@@@@@@@@@
R_EndRegistration

@@@@@@@@@@@@@@@@@@@@@
*/
void R_EndRegistration (void)
{
	int		i;
	model_t	*mod;

	for (i=0, mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (mod->registration_sequence != registration_sequence)
		{	// don't need this model
			Hunk_Free (mod->extradata);
			memset (mod, 0, sizeof(*mod));
		}
		else
		{	// make sure it is paged in
			Com_PageInMemory (mod->extradata, mod->extradatasize);
		}
	}

	R_FreeUnusedImages ();
}


//=============================================================================

/*
================
Mod_Free
================
*/
void Mod_Free (model_t *mod)
{
	Hunk_Free (mod->extradata);
	memset (mod, 0, sizeof(*mod));
}

/*
================
Mod_FreeAll
================
*/
void Mod_FreeAll (void)
{
	int		i;

	for (i=0 ; i<mod_numknown ; i++)
	{
		if (mod_known[i].extradatasize)
			Mod_Free (&mod_known[i]);
	}
}
