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

