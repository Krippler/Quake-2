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
// r_light.c

#include "r_local.h"

int	r_dlightframecount;

static void R_ShadowLightPoint (vec3_t p, vec3_t color);


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	mplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	
	if (node->contents != -1)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;
	
//=====
//PGM
	i=light->intensity;
	if(i<0)
		i=-i;
//PGM
//=====

	if (dist > i)	// PGM (dist > light->intensity)
	{
		R_MarkLights (light, bit, node->children[0]);
		return;
	}
	if (dist < -i)	// PGM (dist < -light->intensity)
	{
		R_MarkLights (light, bit, node->children[1]);
		return;
	}
		
// mark the polygons
	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->dlightframe != r_dlightframecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = r_dlightframecount;
		}
		surf->dlightbits |= bit;
	}

	R_MarkLights (light, bit, node->children[0]);
	R_MarkLights (light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (model_t *model)
{
	int		i;
	dlight_t	*l;

	r_dlightframecount = r_framecount;
	for (i=0, l = r_newrefdef.dlights ; i<r_newrefdef.num_dlights ; i++, l++)
	{
		R_MarkLights ( l, 1<<i, 
			model->nodes + model->firstnode);
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

vec3_t	pointcolor;
mplane_t		*lightplane;		// used as shadow plane
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	int			side;
	mplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	float		*scales;
	int			maps;
	float		samp;
	int			r;

	if (node->contents != -1)
		return -1;		// didn't hit anything
	
// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;
	
	if ( (back < 0) == side)
		return RecursiveLightPoint (node->children[side], start, end);
	
	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;
	if (plane->type < 3)	// axial planes
		mid[plane->type] = plane->dist;

// go down front side	
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something
		
	if ( (back < 0) == side )
		return -1;		// didn't hit anuthing
		
// check for impact on this node
	VectorCopy (mid, lightspot);
	lightplane = plane;

	surf = r_worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags&(SURF_DRAWTURB|SURF_DRAWSKY)) 
			continue;	// no lightmaps

		tex = surf->texinfo;
		
		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];
		if (s < surf->texturemins[0] ||
		t < surf->texturemins[1])
			continue;
		
		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];
		
		if ( ds > surf->extents[0] || dt > surf->extents[1] )
			continue;

		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		VectorCopy (vec3_origin, pointcolor);
		if (lightmap)
		{
			lightmap += dt * ((surf->extents[0]>>4)+1) + ds;

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				samp = *lightmap * /* 0.5 * */ (1.0/255);	// adjust for gl scale
				scales = r_newrefdef.lightstyles[surf->styles[maps]].rgb;
				VectorMA (pointcolor, samp, scales, pointcolor);
				lightmap += ((surf->extents[0]>>4)+1) *
						((surf->extents[1]>>4)+1);
			}
		}
		
		return 1;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

/*
===============
R_LightPoint
===============
*/
void R_LightPoint (vec3_t p, vec3_t color)
{
	vec3_t		end;
	float		r;
	int			lnum;
	dlight_t	*dl;
	float		light;
	vec3_t		dist;
	float		add;
	
	if (!r_worldmodel->lightdata)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}
	
	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;
	
	r = RecursiveLightPoint (r_worldmodel->nodes, p, end);
	
	if (r == -1)
	{
		VectorCopy (vec3_origin, color);
	}
	else
	{
		VectorCopy (pointcolor, color);
	}

	//
	// add dynamic lights
	//
	light = 0;
	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
	{
		dl = &r_newrefdef.dlights[lnum];
		VectorSubtract (currententity->origin,
						dl->origin,
						dist);
		add = dl->intensity - VectorLength(dist);
		add *= (1.0/256);
		if (add > 0)
		{
			VectorMA (color, add, dl->color, color);
		}
	}

	R_ShadowLightPoint (currententity->origin, color);
}

//===================================================================


unsigned		blocklights[1024];	// allow some very large lightmaps

/*
===============
R_AddDynamicLights
===============
*/
/*
With rgb, into blockrgb, each light in its colour; otherwise into the
brightness of blocklights, as id wrote it.
*/
static void R_AddDynamicLightsTo (qboolean rgb);
static void R_AddShadowLightsTo (qboolean rgb);

void R_AddDynamicLights (void)
{
	R_AddDynamicLightsTo (false);
}

int		blockrgb[1024*3];	// sw_colorlight's lighting, before blocktint
int		blocktint[1024*3];	// and each sample's colour, 8.8 of its brightest
qboolean	r_surfcolored;	// blocktint is to be drawn (r_surf.c)

static void R_AddDynamicLightsTo (qboolean rgb)
{
	msurface_t *surf;
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t	*tex;
	dlight_t	*dl;
	int			negativeLight;	//PGM
	int			c, add, *p;

	surf = r_drawsurf.surf;
	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;

	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
	{
		if ( !(surf->dlightbits & (1<<lnum) ) )
			continue;		// not lit by this light

		dl = &r_newrefdef.dlights[lnum];
		rad = dl->intensity;

//=====
//PGM
		negativeLight = 0;
		if(rad < 0)
		{
			negativeLight = 1;
			rad = -rad;
		}
//PGM
//=====

		dist = DotProduct (dl->origin, surf->plane->normal) -
				surf->plane->dist;
		rad -= fabs(dist);
		minlight = 32;		// dl->minlight;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = dl->origin[i] -
					surf->plane->normal[i]*dist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];
		
		for (t = 0 ; t<tmax ; t++)
		{
			td = local[1] - t*16;
			if (td < 0)
				td = -td;
			for (s=0 ; s<smax ; s++)
			{
				sd = local[0] - s*16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td>>1);
				else
					dist = td + (sd>>1);
				if (rgb)
				{
					if (dist >= minlight)
						continue;
					add = (rad - dist)*256;
					p = &blockrgb[(t*smax + s)*3];
					for (c=0 ; c<3 ; c++)
					{
						if (!negativeLight)
							p[c] += add * dl->color[c];
						else if ((p[c] -= add * dl->color[c]) < 0)
							p[c] = 0;
					}
					continue;
				}
//====
//PGM
				if(!negativeLight)
				{
					if (dist < minlight)
						blocklights[t*smax + s] += (rad - dist)*256;
				}
				else
				{
					if (dist < minlight)
						blocklights[t*smax + s] -= (rad - dist)*256;
					if(blocklights[t*smax + s] < minlight)
						blocklights[t*smax + s] = minlight;
				}
//PGM
//====
			}
		}
	}
}

/*
===============
R_BuildColorLight

sw_colorlight: the lighting of a surface whose lightmap has colour in it, or
that a coloured dynamic light falls on, in red, green and blue. The palette
has 256 colours and the colormap darkens and brightens each of them, but has
no tints; so the brightness of each sample stays in blocklights, the
brightest of its three channels, for the colormap, and its colour goes into
blocktint as each channel's share of that, for R_DrawSurface to tint the lit
texel by and to find the nearest colour of the palette to. A surface that
turns out white throughout is left to the colormap alone, as in 1997.
sw_colorlight between 0 and 1 is how much of the colour to keep.
===============
*/
static void R_BuildColorLight (int size)
{
	msurface_t	*surf = r_drawsurf.surf;
	byte		*lightmap;
	int			i, c, maps, hi, *p, *q;
	float		scale[3], keep;
	dlight_t	*dl;
	qboolean	dlit = false;

	r_surfcolored = false;
	keep = sw_colorlight->value;
	if (keep <= 0 || !surf->samplesrgb)
		return;
	if (keep > 1)
		keep = 1;

	// a dynamic light of a colour, one of a muzzle flash's or a blaster's
	if (surf->dlightframe == r_framecount)
		for (i=0 ; i<r_newrefdef.num_dlights ; i++)
		{
			dl = &r_newrefdef.dlights[i];
			if ((surf->dlightbits & (1<<i)) && (dl->color[0] != dl->color[1]
				|| dl->color[1] != dl->color[2]))
				dlit = true;
		}
	if (!surf->colored && !dlit)
		return;

	for (i=0 ; i<size*3 ; i++)
		blockrgb[i] = 0;
	lightmap = surf->samplesrgb;
	for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ; maps++)
	{
		// as r_drawsurf.lightadj, white*128, the white being r+g+b
		for (c=0 ; c<3 ; c++)
			scale[c] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[c]*3*128;
		for (i=0, p = blockrgb ; i<size*3 ; i+=3, p+=3)
			for (c=0 ; c<3 ; c++)
				p[c] += lightmap[i+c] * scale[c];
		lightmap += size*3;
	}
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLightsTo (true);
	if (surf->slframe == r_framecount)
		R_AddShadowLightsTo (true);

	for (i=0, p = blockrgb, q = blocktint ; i<size ; i++, p+=3, q+=3)
	{
		hi = p[0] > p[1] ? p[0] : p[1];
		if (p[2] > hi)
			hi = p[2];
		for (c=0 ; c<3 ; c++)
		{
			q[c] = hi > 0 ? (int)(((long long)p[c] << 16) / hi) : 1 << 16;
			q[c] = (1 << 16) - (int)(((1 << 16) - q[c]) * keep);
			if (q[c] < 0xF000)
				r_surfcolored = true;
		}
	}
	if (!r_surfcolored)
		return;
	for (i=0 ; i<size ; i++)
	{
		p = &blockrgb[i*3];
		hi = p[0] > p[1] ? p[0] : p[1];
		blocklights[i] = p[2] > hi ? p[2] : hi;
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_BuildLightMap (void)
{
	int			smax, tmax;
	int			t;
	int			i, size;
	byte		*lightmap;
	unsigned	scale;
	int			maps;
	msurface_t	*surf;

	surf = r_drawsurf.surf;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;

	r_surfcolored = false;
	if (r_fullbright->value || !r_worldmodel->lightdata)
	{
		for (i=0 ; i<size ; i++)
			blocklights[i] = 0;
		return;
	}

// clear to no light
	for (i=0 ; i<size ; i++)
		blocklights[i] = 0;


// add all the lightmaps
	lightmap = surf->samples;
	if (lightmap)
		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			scale = r_drawsurf.lightadj[maps];	// 8.8 fraction		
			for (i=0 ; i<size ; i++)
				blocklights[i] += lightmap[i] * scale;
			lightmap += size;	// skip to next lightmap
		}

// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights ();
	if (surf->slframe == r_framecount)
		R_AddShadowLightsTo (false);

	R_BuildColorLight (size);

// bound, invert, and shift
	for (i=0 ; i<size ; i++)
	{
		t = (int)blocklights[i];
		if (t < 0)
			t = 0;
		t = (255*256 - t) >> (8 - VID_CBITS);

		if (t < (1 << 6))
			t = (1 << 6);

		blocklights[i] = t;
	}
}


/*
=============================================================================

THE RE-RELEASE'S REAL-TIME LIGHTS

The 2023 re-release lights its remade levels with lights of its own as
well as their lightmaps, which its renderer draws with shadows. Here they
are white light added to the lightmaps of the walls each can see, falling
off to nothing at its radius, and to the models it can see; a spotlight
only within its cone. A light behind a wall does not shine through it, as
1997's dynamic lights do, nor past a pillar or a ledge onto what is behind
it: which of each surface's lightmap samples a light can see is worked out
the first time the light is in view, by a line through the BSP from the
light to each, and kept until the map changes. So a light's shadows fall
where its lightmap samples do, 16 units apart, and the surface cache's
blending between samples softens their edges, as the lightmaps' own are.

The lights do not move, so a lit surface stays in the surface cache as
long as the lights on it keep their brightness: it is the slkey a surface
is marked with, of which lights are on it and how bright, that tells
D_CacheSurface whether to light it again, rather than a dynamic light's
every frame.
=============================================================================
*/

typedef struct
{
	model_t		*world;		// what it was worked out in, and when:
	int			sequence;	// a new map can be in the same model_t
	vec3_t		origin;		// as given
	float		radius;
	vec3_t		from;		// where lines are drawn from: out of a wall's solid
	byte		*vis;		// the world's surfaces it can see, a bit each
	int			*list;		// the same, by number, lowest first
	int			*litofs;	// for each of those, where its samples' bits start
	byte		*lit;		// a bit a lightmap sample, set where it sees the light
	int			numlist;
} rshadowlight_t;

static rshadowlight_t	r_slcache[MAX_SHADOWLIGHT_CS+1];	// by id
static shadowlight_t	*r_sl[MAX_SHADOWLIGHTS];		// this frame's, with their cache
static rshadowlight_t	*r_slc[MAX_SHADOWLIGHTS];
static int				r_numsl;

/*
=============
R_SampleGrid

Where a surface's lightmap samples are in the world: the first at p0, on
its plane where the texture axes give its texturemins, and each next one
ds along s and dt along t, 16 texture units apart.
=============
*/
static qboolean R_SampleGrid (msurface_t *surf, vec3_t p0, vec3_t ds, vec3_t dt)
{
	mtexinfo_t	*tex = surf->texinfo;
	vec3_t		c1, c2, c3;
	float		det, a0, b0;
	int			i;

	CrossProduct (tex->vecs[1], surf->plane->normal, c1);
	CrossProduct (surf->plane->normal, tex->vecs[0], c2);
	CrossProduct (tex->vecs[0], tex->vecs[1], c3);
	det = DotProduct (tex->vecs[0], c1);
	if (fabs (det) < 1e-6)
		return false;
	a0 = surf->texturemins[0] - tex->vecs[0][3];
	b0 = surf->texturemins[1] - tex->vecs[1][3];
	for (i=0 ; i<3 ; i++)
	{
		p0[i] = (a0*c1[i] + b0*c2[i] + surf->plane->dist*c3[i]) / det;
		ds[i] = 16 * c1[i] / det;
		dt[i] = 16 * c2[i] / det;
	}
	return true;
}

// whether the line from start to end goes through solid
static qboolean R_LineBlocked (mnode_t *node, vec3_t start, vec3_t end)
{
	float	front, back, frac;
	vec3_t	mid;
	int		side;

	while (node->contents == CONTENTS_NODE)
	{
		front = DotProduct (start, node->plane->normal) - node->plane->dist;
		back = DotProduct (end, node->plane->normal) - node->plane->dist;
		if (front >= 0 && back >= 0)
		{
			node = node->children[0];
			continue;
		}
		if (front < 0 && back < 0)
		{
			node = node->children[1];
			continue;
		}
		side = front < 0;
		frac = front / (front - back);
		mid[0] = start[0] + frac*(end[0] - start[0]);
		mid[1] = start[1] + frac*(end[1] - start[1]);
		mid[2] = start[2] + frac*(end[2] - start[2]);
		if (R_LineBlocked (node->children[side], start, mid))
			return true;
		start = mid;
		node = node->children[!side];
	}
	return (((mleaf_t *)node)->contents & CONTENTS_SOLID) != 0;
}

static qboolean R_PointSolid (vec3_t p)
{
	mnode_t	*node = r_worldmodel->nodes;

	while (node->contents == CONTENTS_NODE)
		node = node->children[DotProduct (p, node->plane->normal) - node->plane->dist < 0];
	return (((mleaf_t *)node)->contents & CONTENTS_SOLID) != 0;
}

static void R_ShadowLightCache (rshadowlight_t *c, shadowlight_t *sl)
{
	static float	nudge[6][3] = {{0,0,-1},{0,0,1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
	msurface_t	*surf;
	medge_t		*edge;
	vec3_t		v[64], centre, p, q, d, p0, ds, dt;
	float		dist, size, side;
	int			i, j, k, e, n, s, t, smax, tmax, surfnum, numlit, maxlit, any;
	byte		*grow;

	if (c->vis)
		free (c->vis);
	if (c->list)
		free (c->list);
	if (c->litofs)
		free (c->litofs);
	if (c->lit)
		free (c->lit);
	memset (c, 0, sizeof(*c));
	c->world = r_worldmodel;
	c->sequence = registration_sequence;
	VectorCopy (sl->origin, c->origin);
	c->radius = sl->radius;

	// a lamp's light is often put in the lamp
	VectorCopy (sl->origin, c->from);
	for (k=1 ; k<=4 && R_PointSolid (c->from) ; k++)
		for (i=0 ; i<6 ; i++)
		{
			VectorMA (sl->origin, k*4, nudge[i], c->from);
			if (!R_PointSolid (c->from))
				break;
		}

	c->vis = calloc ((r_worldmodel->numsurfaces + 7) >> 3, 1);
	c->list = malloc (r_worldmodel->nummodelsurfaces * sizeof(int));
	c->litofs = malloc (r_worldmodel->nummodelsurfaces * sizeof(int));
	maxlit = 4096;
	c->lit = malloc (maxlit);
	numlit = 0;
	if (!c->vis || !c->list || !c->litofs || !c->lit)
	{
		free (c->vis);
		c->vis = NULL;
		return;
	}

	for (i=0 ; i<r_worldmodel->nummodelsurfaces ; i++)
	{
		surfnum = r_worldmodel->firstmodelsurface + i;
		surf = r_worldmodel->surfaces + surfnum;
		if (surf->flags & (SURF_DRAWSKY|SURF_DRAWTURB|SURF_DRAWSKYBOX) || !surf->samples)
			continue;

		// facing it, near enough
		side = (surf->flags & SURF_PLANEBACK) ? -1 : 1;
		dist = side * (DotProduct (c->from, surf->plane->normal) - surf->plane->dist);
		if (dist <= 0 || dist >= c->radius)
			continue;

		n = surf->numedges < 64 ? surf->numedges : 64;
		VectorClear (centre);
		for (j=0 ; j<n ; j++)
		{
			e = r_worldmodel->surfedges[surf->firstedge + j];
			edge = &r_worldmodel->edges[e >= 0 ? e : -e];
			VectorCopy (r_worldmodel->vertexes[edge->v[e >= 0 ? 0 : 1]].position, v[j]);
			VectorAdd (centre, v[j], centre);
		}
		if (!n)
			continue;
		VectorScale (centre, 1.0/n, centre);
		size = 0;
		for (j=0 ; j<n ; j++)
		{
			VectorSubtract (v[j], centre, d);
			if (VectorLength (d) > size)
				size = VectorLength (d);
		}
		VectorSubtract (centre, c->from, d);
		if (VectorLength (d) - size >= c->radius)
			continue;
		if (!R_SampleGrid (surf, p0, ds, dt))
			continue;
		VectorMA (centre, side, surf->plane->normal, centre);

		// each sample, a unit off the face, that sees it; one that is in
		// solid, as those past the edge of a face can be, is moved in
		// toward the face's middle until it is not, as qrad does
		smax = (surf->extents[0]>>4)+1;
		tmax = (surf->extents[1]>>4)+1;
		if (numlit + ((smax*tmax + 7) >> 3) > maxlit)
		{
			maxlit = (numlit + ((smax*tmax + 7) >> 3)) * 2;
			grow = realloc (c->lit, maxlit);
			if (!grow)
				break;
			c->lit = grow;
		}
		memset (c->lit + numlit, 0, (smax*tmax + 7) >> 3);
		any = 0;
		for (t=0 ; t<tmax ; t++)
			for (s=0 ; s<smax ; s++)
			{
				for (k=0 ; k<3 ; k++)
					p[k] = p0[k] + s*ds[k] + t*dt[k] + side*surf->plane->normal[k];
				VectorSubtract (p, sl->origin, d);
				if (VectorLength (d) >= sl->radius)
					continue;
				for (k=1 ; k<=4 && R_PointSolid (p) ; k++)
				{
					VectorSubtract (centre, p, d);
					VectorMA (p, 0.25 * k, d, q);
					if (!R_PointSolid (q))
						VectorCopy (q, p);
				}
				if (R_LineBlocked (r_worldmodel->nodes, c->from, p))
					continue;
				j = t*smax + s;
				c->lit[numlit + (j >> 3)] |= 1 << (j & 7);
				any = 1;
			}
		if (!any)
			continue;

		c->vis[surfnum >> 3] |= 1 << (surfnum & 7);
		c->litofs[c->numlist] = numlit;
		c->list[c->numlist++] = surfnum;
		numlit += (smax*tmax + 7) >> 3;
	}
}

/*
=============
R_PushShadowLights

Before the world is drawn: each light's cache, made if it is not there,
and the surfaces it falls on marked with it.
=============
*/
void R_PushShadowLights (void)
{
	shadowlight_t	*sl;
	rshadowlight_t	*c;
	msurface_t		*surf;
	unsigned		key;
	int				i, j;

	r_numsl = 0;
	if (!r_worldmodel || !r_worldmodel->lightdata || r_fullbright->value)
		return;

	for (i=0, sl=r_newrefdef.shadowlights ; i<r_newrefdef.num_shadowlights ; i++, sl++)
	{
		if (sl->id <= 0 || sl->id > MAX_SHADOWLIGHT_CS || r_numsl == MAX_SHADOWLIGHTS)
			continue;
		c = &r_slcache[sl->id];
		if (c->world != r_worldmodel || c->sequence != registration_sequence
			|| !VectorCompare (c->origin, sl->origin) || c->radius != sl->radius)
			R_ShadowLightCache (c, sl);
		if (!c->vis)
			continue;
		r_sl[r_numsl] = sl;
		r_slc[r_numsl++] = c;

		// which light, how bright, and pointing where, for the cache
		key = sl->id * 2654435761u ^ (unsigned)(sl->brightness * 4);
		key = key * 2246822519u ^ (unsigned)(sl->spot[0]*64 + sl->spot[1]*4096 + sl->spot[2]*262144);
		for (j=0 ; j<c->numlist ; j++)
		{
			surf = r_worldmodel->surfaces + c->list[j];
			if (surf->slframe != r_framecount)
			{
				surf->slframe = r_framecount;
				surf->slkey = 0;
			}
			surf->slkey = (surf->slkey + key) * 3266489917u + 1;
		}
	}
}

/*
=============
R_AddShadowLightsTo

Each light marked on r_drawsurf's surface, into its lightmap: blocklights,
or with rgb blockrgb, in the same 8.8 a lightmap's brightness is.
=============
*/
static void R_AddShadowLightsTo (qboolean rgb)
{
	msurface_t		*surf = r_drawsurf.surf;
	shadowlight_t	*sl;
	rshadowlight_t	*sc;
	vec3_t			n, p0, ds, dt, p, d;
	float			planedist, dist, f, cosang, spot;
	int				i, s, t, smax, tmax, surfnum, add, c, *q, lo, hi, mid;
	byte			*lit;

	surfnum = surf - r_worldmodel->surfaces;
	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	VectorCopy (surf->plane->normal, n);
	if (!R_SampleGrid (surf, p0, ds, dt))
		return;

	for (i=0 ; i<r_numsl ; i++)
	{
		sc = r_slc[i];
		if (!(sc->vis[surfnum >> 3] & (1 << (surfnum & 7))))
			continue;
		sl = r_sl[i];

		// which of its samples see the light
		lo = 0;
		hi = sc->numlist - 1;
		while (lo < hi)
		{
			mid = (lo + hi) >> 1;
			if (sc->list[mid] < surfnum)
				lo = mid + 1;
			else
				hi = mid;
		}
		if (sc->list[lo] != surfnum)
			continue;
		lit = sc->lit + sc->litofs[lo];
		planedist = DotProduct (sc->from, n) - surf->plane->dist;
		if (surf->flags & SURF_PLANEBACK)
			planedist = -planedist;
		if (planedist <= 0)
			continue;

		for (t=0 ; t<tmax ; t++)
			for (s=0 ; s<smax ; s++)
			{
				if (!(lit[(t*smax + s) >> 3] & (1 << ((t*smax + s) & 7))))
					continue;
				for (c=0 ; c<3 ; c++)
					p[c] = p0[c] + s*ds[c] + t*dt[c];
				VectorSubtract (p, sl->origin, d);
				dist = VectorLength (d);
				if (dist >= sl->radius)
					continue;
				// falling off to its radius, and less at a slant
				cosang = dist > 1 ? planedist / dist : 1;
				f = (1 - dist / sl->radius) * (0.35 + 0.65 * cosang);
				if (sl->spot[0] || sl->spot[1] || sl->spot[2])
				{
					spot = dist > 1 ? DotProduct (d, sl->spot) / dist : 1;
					if (spot <= sl->spotouter)
						continue;
					if (spot < sl->spotinner)
						f *= (spot - sl->spotouter) / (sl->spotinner - sl->spotouter);
				}
				add = (int)(sl->brightness * f * 256);
				if (add <= 0)
					continue;
				if (rgb)
				{
					q = &blockrgb[(t*smax + s)*3];
					q[0] += add;
					q[1] += add;
					q[2] += add;
				}
				else
					blocklights[t*smax + s] += add;
			}
	}
}

/*
=============
R_ShadowLightPoint

A model's light at p from the lights that can see it: added to color, in
R_LightPoint's 0 to 1 of a lightmap's brightness.
=============
*/
static void R_ShadowLightPoint (vec3_t p, vec3_t color)
{
	shadowlight_t	*sl;
	vec3_t			d, mid;
	float			dist, f, spot;
	int				i;

	for (i=0 ; i<r_numsl ; i++)
	{
		sl = r_sl[i];
		VectorSubtract (p, sl->origin, d);
		dist = VectorLength (d);
		if (dist >= sl->radius)
			continue;
		f = 1 - dist / sl->radius;
		if (sl->spot[0] || sl->spot[1] || sl->spot[2])
		{
			spot = dist > 1 ? DotProduct (d, sl->spot) / dist : 1;
			if (spot <= sl->spotouter)
				continue;
			if (spot < sl->spotinner)
				f *= (spot - sl->spotouter) / (sl->spotinner - sl->spotouter);
		}
		// a model's origin is often at its feet, in the floor's face
		VectorCopy (p, mid);
		mid[2] += 16;
		if (R_LineBlocked (r_worldmodel->nodes, r_slc[i]->from, mid))
			continue;
		f *= sl->brightness / 255;
		color[0] += f;
		color[1] += f;
		color[2] += f;
	}
}
