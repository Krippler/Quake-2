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
r_md5.c -- the 2023 re-release's skeletal models, as alias frames

Next to many of its .md2 models the re-release has an md5/ directory: the
same model with more polygons, as a Doom 3 md5mesh, its frames in an
md5anim, frame for frame the .md2's, and its skins as .png at more pixels.
Software has no skeletal drawing, so each frame is posed and skinned here,
once, at load, and the whole put back together as an .md2 that
Mod_LoadAliasModel then takes as it would any other: one vertex for each of
the mesh's, each frame's positions quantized to a byte, as an .md2's are,
its normals the nearest of the 162 the lighting has.

Some joints shrink on some frames (a .md5scale, JSON: joint, then frame and
scale, and whether its position too scales); gibbed limbs mostly.

The way q2pro-ng (GPL) loads them, models.c, was the reference.
*/

#include <setjmp.h>
#include "r_local.h"

#define MD5_MAX_JOINTS	256
#define MD5_MAX_FRAMES	1024

cvar_t	*sw_md5;

extern float	r_avertexnormals[NUMVERTEXNORMALS][3];

typedef struct
{
	char	name[64];
	int		parent, flags, start;
	qboolean	scale_pos;
	vec3_t	pos;		// the base frame
	float	q[4];
} md5joint_t;

typedef struct
{
	int		joint;
	float	bias;
	vec3_t	pos;
} md5weight_t;

typedef struct
{
	float	st[2];
	int		start, count;
} md5vert_t;

static jmp_buf		md5_jmp;
static const char	*md5_path;

static md5joint_t	*joints;
static int			numjoints;
static md5vert_t	*verts;
static int			numverts;
static md5weight_t	*weights;
static int			numweights;
static int			*tris;
static int			numtris;

static void MD5_strncpyz (char *dest, char *src, int size)
{
	strncpy (dest, src, size - 1);
	dest[size - 1] = 0;
}

static void MD5_Error (char *msg)
{
	ri.Con_Printf (PRINT_ALL, "%s: %s\n", md5_path, msg);
	longjmp (md5_jmp, 1);
}

static char *MD5_Token (char **s)
{
	char	*t = COM_Parse (s);

	if (!*s && !t[0])
		MD5_Error ("unexpected end of file");
	return t;
}

static void MD5_Expect (char **s, char *what)
{
	if (strcmp (MD5_Token (s), what))
		MD5_Error (va ("expected %s", what));
}

static int MD5_Int (char **s, int min, int max)
{
	int		v = atoi (MD5_Token (s));

	if (v < min || v > max)
		MD5_Error ("number out of range");
	return v;
}

static float MD5_Float (char **s)
{
	return atof (MD5_Token (s));
}

static void MD5_Vector (char **s, vec3_t v)
{
	MD5_Expect (s, "(");
	v[0] = MD5_Float (s);
	v[1] = MD5_Float (s);
	v[2] = MD5_Float (s);
	MD5_Expect (s, ")");
}

// an orientation's w, from its unit length
static void Quat_W (float q[4])
{
	float	t = 1 - q[0]*q[0] - q[1]*q[1] - q[2]*q[2];

	q[3] = t < 0 ? 0 : -sqrt (t);
}

static void Quat_Mul (float a[4], float b[4], float out[4])
{
	float	r[4], len;

	r[0] = a[0]*b[3] + a[3]*b[0] + a[1]*b[2] - a[2]*b[1];
	r[1] = a[1]*b[3] + a[3]*b[1] + a[2]*b[0] - a[0]*b[2];
	r[2] = a[2]*b[3] + a[3]*b[2] + a[0]*b[1] - a[1]*b[0];
	r[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
	len = sqrt (r[0]*r[0] + r[1]*r[1] + r[2]*r[2] + r[3]*r[3]);
	if (len)
		len = 1 / len;
	out[0] = r[0]*len; out[1] = r[1]*len; out[2] = r[2]*len; out[3] = r[3]*len;
}

// v + 2w(u x v) + 2u x (u x v)
static void Quat_Rotate (float q[4], vec3_t v, vec3_t out)
{
	vec3_t	t, c;

	CrossProduct (q, v, t);
	VectorScale (t, 2, t);
	CrossProduct (q, t, c);
	out[0] = v[0] + q[3]*t[0] + c[0];
	out[1] = v[1] + q[3]*t[1] + c[1];
	out[2] = v[2] + q[3]*t[2] + c[2];
}

/*
The mesh: its joints (only their names are kept; the anim's base frame
poses them), and its meshes, their vertices, triangles and weights one list.
*/
static void MD5_ParseMesh (char *s)
{
	int		i, j, nummeshes, m;
	int		firstvert, firstweight, mverts, mtris, mweights;

	MD5_Expect (&s, "MD5Version");
	MD5_Expect (&s, "10");
	MD5_Expect (&s, "commandline");
	MD5_Token (&s);
	MD5_Expect (&s, "numJoints");
	numjoints = MD5_Int (&s, 1, MD5_MAX_JOINTS);
	MD5_Expect (&s, "numMeshes");
	nummeshes = MD5_Int (&s, 1, 32);

	joints = calloc (numjoints, sizeof(*joints));
	MD5_Expect (&s, "joints");
	MD5_Expect (&s, "{");
	for (i=0 ; i<numjoints ; i++)
	{
		vec3_t	skip;

		MD5_strncpyz (joints[i].name, MD5_Token (&s), sizeof(joints[i].name));
		MD5_Int (&s, -1, numjoints - 1);
		MD5_Vector (&s, skip);
		MD5_Vector (&s, skip);
	}
	MD5_Expect (&s, "}");

	for (m=0 ; m<nummeshes ; m++)
	{
		MD5_Expect (&s, "mesh");
		MD5_Expect (&s, "{");
		MD5_Expect (&s, "shader");
		MD5_Token (&s);

		firstvert = numverts;
		MD5_Expect (&s, "numverts");
		mverts = MD5_Int (&s, 0, MAX_ALIAS_VERTS - numverts);
		numverts += mverts;
		verts = realloc (verts, numverts * sizeof(*verts));
		for (i=0 ; i<mverts ; i++)
		{
			md5vert_t	*v;

			MD5_Expect (&s, "vert");
			v = &verts[firstvert + MD5_Int (&s, 0, mverts - 1)];
			MD5_Expect (&s, "(");
			v->st[0] = MD5_Float (&s);
			v->st[1] = MD5_Float (&s);
			MD5_Expect (&s, ")");
			v->start = MD5_Int (&s, 0, 0xffff);
			v->count = MD5_Int (&s, 0, 0xffff);
		}

		MD5_Expect (&s, "numtris");
		mtris = MD5_Int (&s, 0, MAX_ALIAS_TRIS - numtris);
		tris = realloc (tris, (numtris + mtris) * 3 * sizeof(*tris));
		for (i=0 ; i<mtris ; i++)
		{
			int		*t;

			MD5_Expect (&s, "tri");
			t = &tris[(numtris + MD5_Int (&s, 0, mtris - 1)) * 3];
			for (j=0 ; j<3 ; j++)
				t[j] = firstvert + MD5_Int (&s, 0, mverts - 1);
		}
		numtris += mtris;

		firstweight = numweights;
		MD5_Expect (&s, "numweights");
		mweights = MD5_Int (&s, 0, 0x100000);
		numweights += mweights;
		weights = realloc (weights, numweights * sizeof(*weights));
		for (i=0 ; i<mweights ; i++)
		{
			md5weight_t	*w;

			MD5_Expect (&s, "weight");
			w = &weights[firstweight + MD5_Int (&s, 0, mweights - 1)];
			w->joint = MD5_Int (&s, 0, numjoints - 1);
			w->bias = MD5_Float (&s);
			MD5_Vector (&s, w->pos);
		}
		MD5_Expect (&s, "}");

		for (i=firstvert ; i<numverts ; i++)
		{
			if (verts[i].start + verts[i].count > mweights)
				MD5_Error ("bad vertex weights");
			verts[i].start += firstweight;
		}
	}

	if (!numverts || !numtris)
		MD5_Error ("no triangles");
}

/*
The .md5scale, JSON: { "joint": { "frame": scale, ..., "scale_positions":
true }, ... }. Its punctuation blanked, it is a joint's name, then keys and
values, then the next joint's name: a key is a number or scale_positions.
*/
static void MD5_LoadScales (char *path, float *scales, int numframes)
{
	char	*buf, *s, *t, key[64];
	int		len, j, frame;

	len = ri.FS_LoadFile (path, (void **)&buf);
	if (!buf)
		return;
	s = malloc (len + 1);
	memcpy (s, buf, len);
	s[len] = 0;
	ri.FS_FreeFile (buf);
	buf = s;

	for (t=s ; *t ; t++)
		if (*t == ':' || *t == ',' || *t == '{' || *t == '}')
			*t = ' ';

	j = -1;
	for (s=buf ; ; )
	{
		t = COM_Parse (&s);
		if (!s && !t[0])
			break;
		MD5_strncpyz (key, t, sizeof(key));

		if (strcmp (key, "scale_positions") && (key[0] < '0' || key[0] > '9'))
		{
			for (j=0 ; j<numjoints ; j++)
				if (!strcmp (joints[j].name, key))
					break;
			if (j == numjoints)
			{
				ri.Con_Printf (PRINT_DEVELOPER, "%s: no joint %s\n", path, key);
				j = -1;
			}
			continue;
		}

		t = COM_Parse (&s);
		if (j < 0)
			continue;
		if (!strcmp (key, "scale_positions"))
			joints[j].scale_pos = !strcmp (t, "true");
		else
		{
			frame = atoi (key);
			if (frame < numframes)
				scales[frame * numjoints + j] = atof (t);
		}
		if (!s)
			break;
	}
	free (buf);
}

// the nearest of the lighting's normals, from a 16x16x16 grid of directions
static byte	md5_normalgrid[16][16][16];
static qboolean	md5_normalgrid_built;

static void MD5_BuildNormalGrid (void)
{
	int		x, y, z, i, best;
	float	d, bestd;
	vec3_t	n;

	for (x=0 ; x<16 ; x++)
		for (y=0 ; y<16 ; y++)
			for (z=0 ; z<16 ; z++)
			{
				VectorSet (n, x/7.5f - 1, y/7.5f - 1, z/7.5f - 1);
				VectorNormalize (n);
				best = 0;
				bestd = -2;
				for (i=0 ; i<NUMVERTEXNORMALS ; i++)
				{
					d = DotProduct (n, r_avertexnormals[i]);
					if (d > bestd)
					{
						bestd = d;
						best = i;
					}
				}
				md5_normalgrid[x][y][z] = best;
			}
	md5_normalgrid_built = true;
}

static int MD5_NormalIndex (vec3_t n)
{
	int		g[3], i;

	for (i=0 ; i<3 ; i++)
	{
		g[i] = (int)((n[i] + 1) * 7.5f + 0.5f);
		if (g[i] < 0)
			g[i] = 0;
		else if (g[i] > 15)
			g[i] = 15;
	}
	return md5_normalgrid[g[0]][g[1]][g[2]];
}

static vec3_t	*md5_sortpos;

static int MD5_ComparePos (const void *a, const void *b)
{
	float	*pa = md5_sortpos[*(int *)a], *pb = md5_sortpos[*(int *)b];
	int		i;

	for (i=0 ; i<3 ; i++)
		if (pa[i] != pb[i])
			return pa[i] < pb[i] ? -1 : 1;
	return *(int *)a - *(int *)b;
}

// a model's skin, with md5/ in its path
static void MD5_SkinPath (char *in, char *out, int size)
{
	char	*slash = strrchr (in, '/');

	if (!slash)
		Com_sprintf (out, size, "md5/%s", in);
	else
		Com_sprintf (out, size, "%.*s/md5%s", (int)(slash - in), in, slash);
}

/*
The md5 version of a skin an entity has its own of (a player's), if there is
one; looked for once
*/
image_t *R_MD5Skin (image_t *skin)
{
	char	name[MAX_QPATH];

	if (!skin->md5tried)
	{
		skin->md5tried = true;
		MD5_SkinPath (skin->name, name, sizeof(name));
		if (strlen (name) < MAX_QPATH - 1)
			skin->md5skin = R_FindImage (name, it_skin);
	}
	return skin->md5skin ? skin->md5skin : skin;
}

static void MD5_Free (void)
{
	free (joints);
	free (verts);
	free (weights);
	free (tris);
	joints = NULL;
	verts = NULL;
	weights = NULL;
	tris = NULL;
	numjoints = numverts = numweights = numtris = 0;
}

/*
=================
R_LoadMD5

The md5 replacement of an .md2 (md2, as read), put together as an .md2 of
its own, or NULL. Freed with free.
=================
*/
void *R_LoadMD5 (char *name, void *md2, int *outlen)
{
	char		meshpath[MAX_QPATH], animpath[MAX_QPATH], path[MAX_QPATH];
	char		skinname[MAX_SKINNAME], *slash;
	// static, as what a longjmp comes back to
	static char		*meshbuf, *animbuf;
	static float	*scales;
	static vec3_t	*pos, *nacc;
	static int		*weld, *order;
	static byte		*out;
	static md5joint_t	*frame;
	char		*s;
	float		components[MD5_MAX_JOINTS * 6];
	dmdl_t		in, *hdr;
	dstvert_t	*st;
	dtriangle_t	*tri;
	daliasframe_t	*fr;
	image_t		*skin;
	int			i, j, k, f, len, numframes, numcomponents, outframes;
	int			skinwidth, skinheight, numskins, framesize;
	qboolean	welded = false;

	if (!sw_md5->value)
		return NULL;

	slash = strrchr (name, '/');
	if (!slash || strcmp (name + strlen (name) - 4, ".md2"))
		return NULL;
	Com_sprintf (meshpath, sizeof(meshpath), "%.*s/md5%.*s.md5mesh",
		(int)(slash - name), name, (int)(strlen (slash) - 4), slash);
	Com_sprintf (animpath, sizeof(animpath), "%.*s/md5%.*s.md5anim",
		(int)(slash - name), name, (int)(strlen (slash) - 4), slash);

	if (ri.FS_LoadFile (meshpath, NULL) <= 0 || ri.FS_LoadFile (animpath, NULL) <= 0)
		return NULL;

	for (i=0 ; i<sizeof(in)/4 ; i++)
		((int *)&in)[i] = LittleLong (((int *)md2)[i]);

	if (!md5_normalgrid_built)
		MD5_BuildNormalGrid ();

	meshbuf = animbuf = NULL;
	scales = NULL;
	pos = nacc = NULL;
	weld = order = NULL;
	out = NULL;
	frame = NULL;

	md5_path = meshpath;
	if (setjmp (md5_jmp))
	{
		free (meshbuf);
		free (animbuf);
		free (scales);
		free (pos);
		free (nacc);
		free (weld);
		free (order);
		free (out);
		free (frame);
		MD5_Free ();
		return NULL;
	}

	// NUL ended copies, for COM_Parse
	len = ri.FS_LoadFile (meshpath, (void **)&s);
	meshbuf = malloc (len + 1);
	memcpy (meshbuf, s, len);
	meshbuf[len] = 0;
	ri.FS_FreeFile (s);
	len = ri.FS_LoadFile (animpath, (void **)&s);
	animbuf = malloc (len + 1);
	memcpy (animbuf, s, len);
	animbuf[len] = 0;
	ri.FS_FreeFile (s);

	MD5_ParseMesh (meshbuf);

	md5_path = animpath;
	s = animbuf;
	MD5_Expect (&s, "MD5Version");
	MD5_Expect (&s, "10");
	MD5_Expect (&s, "commandline");
	MD5_Token (&s);
	MD5_Expect (&s, "numFrames");
	numframes = MD5_Int (&s, 1, MD5_MAX_FRAMES);
	MD5_Expect (&s, "numJoints");
	if (MD5_Int (&s, 1, MD5_MAX_JOINTS) != numjoints)
		MD5_Error ("its joints are not its mesh's");
	MD5_Expect (&s, "frameRate");
	MD5_Token (&s);
	MD5_Expect (&s, "numAnimatedComponents");
	numcomponents = MD5_Int (&s, 0, numjoints * 6);

	MD5_Expect (&s, "hierarchy");
	MD5_Expect (&s, "{");
	for (i=0 ; i<numjoints ; i++)
	{
		md5joint_t	*jt = &joints[i];

		if (strcmp (MD5_Token (&s), jt->name))
			MD5_Error ("its joints are not its mesh's");
		jt->parent = MD5_Int (&s, -1, i - 1);
		jt->flags = MD5_Int (&s, 0, 63);
		jt->start = MD5_Int (&s, 0, numcomponents);
		for (j=k=0 ; j<6 ; j++)
			if (jt->flags & (1<<j))
				k++;
		if (jt->start + k > numcomponents)
			MD5_Error ("bad joint");
	}
	MD5_Expect (&s, "}");

	// bounds: the .md2's do
	MD5_Expect (&s, "bounds");
	MD5_Expect (&s, "{");
	for (i=0 ; i<numframes * 10 ; i++)
		MD5_Token (&s);
	MD5_Expect (&s, "}");

	MD5_Expect (&s, "baseframe");
	MD5_Expect (&s, "{");
	for (i=0 ; i<numjoints ; i++)
	{
		MD5_Vector (&s, joints[i].pos);
		MD5_Vector (&s, joints[i].q);
	}
	MD5_Expect (&s, "}");

	scales = malloc (numframes * numjoints * sizeof(*scales));
	for (i=0 ; i<numframes * numjoints ; i++)
		scales[i] = 1;
	Com_sprintf (path, sizeof(path), "%.*s.md5scale", (int)(strlen (animpath) - 8), animpath);
	MD5_LoadScales (path, scales, numframes);

	// the skins, from md5/ where they are; their st are the first's pixels
	numskins = in.num_skins;
	if (numskins < 0 || numskins > MAX_MD2SKINS)
		MD5_Error ("bad .md2 skins");
	skinwidth = in.skinwidth;
	skinheight = in.skinheight;
	if (numskins)
	{
		MD5_SkinPath ((char *)md2 + in.ofs_skins, path, sizeof(path));
		skin = strlen (path) < MAX_SKINNAME ? R_FindImage (path, it_skin) : NULL;
		if (skin)
		{
			skinwidth = skin->width;
			skinheight = skin->height;
		}
	}
	if (skinwidth <= 0 || skinheight <= 0 || skinheight > MAX_LBM_HEIGHT)
		MD5_Error ("bad skin size");

	// the .md2 put back together
	outframes = numframes > in.num_frames ? numframes : in.num_frames;
	framesize = sizeof(daliasframe_t) - sizeof(dtrivertx_t) + numverts * sizeof(dtrivertx_t);
	len = sizeof(dmdl_t) + numskins * MAX_SKINNAME + numverts * sizeof(dstvert_t)
		+ numtris * sizeof(dtriangle_t) + outframes * framesize;
	out = calloc (1, len);
	hdr = (dmdl_t *)out;
	hdr->ident = IDALIASHEADER;
	hdr->version = ALIAS_VERSION;
	hdr->skinwidth = skinwidth;
	hdr->skinheight = skinheight;
	hdr->framesize = framesize;
	hdr->num_skins = numskins;
	hdr->num_xyz = numverts;
	hdr->num_st = numverts;
	hdr->num_tris = numtris;
	hdr->num_glcmds = 0;
	hdr->num_frames = outframes;
	hdr->ofs_skins = sizeof(dmdl_t);
	hdr->ofs_st = hdr->ofs_skins + numskins * MAX_SKINNAME;
	hdr->ofs_tris = hdr->ofs_st + numverts * sizeof(dstvert_t);
	hdr->ofs_frames = hdr->ofs_tris + numtris * sizeof(dtriangle_t);
	hdr->ofs_glcmds = hdr->ofs_frames + outframes * framesize;
	hdr->ofs_end = hdr->ofs_glcmds;

	for (i=0 ; i<numskins ; i++)
	{
		MD5_strncpyz (skinname, (char *)md2 + in.ofs_skins + i * MAX_SKINNAME, sizeof(skinname));
		MD5_SkinPath (skinname, path, sizeof(path));
		if (strlen (path) < MAX_SKINNAME && R_FindImage (path, it_skin))
			MD5_strncpyz (skinname, path, sizeof(skinname));
		memcpy (out + hdr->ofs_skins + i * MAX_SKINNAME, skinname, MAX_SKINNAME);
	}

	st = (dstvert_t *)(out + hdr->ofs_st);
	for (i=0 ; i<numverts ; i++)
	{
		st[i].s = (int)(verts[i].st[0] * skinwidth + 0.5f);
		st[i].t = (int)(verts[i].st[1] * skinheight + 0.5f);
		st[i].s = st[i].s < 0 ? 0 : st[i].s > skinwidth - 1 ? skinwidth - 1 : st[i].s;
		st[i].t = st[i].t < 0 ? 0 : st[i].t > skinheight - 1 ? skinheight - 1 : st[i].t;
	}

	// md5's triangles turn as an .md2's
	tri = (dtriangle_t *)(out + hdr->ofs_tris);
	for (i=0 ; i<numtris ; i++)
		for (j=0 ; j<3 ; j++)
			tri[i].index_xyz[j] = tri[i].index_st[j] = tris[i*3 + j];

	// the frames, posed and skinned
	frame = malloc (numjoints * sizeof(*frame));
	pos = malloc (numverts * sizeof(*pos));
	nacc = malloc (numverts * sizeof(*nacc));
	weld = malloc (numverts * sizeof(*weld));
	order = malloc (numverts * sizeof(*order));

	for (f=0 ; f<numframes ; f++)
	{
		vec3_t	mins, maxs, scale, e1, e2, n;
		int		fn;

		MD5_Expect (&s, "frame");
		fn = MD5_Int (&s, 0, numframes - 1);
		MD5_Expect (&s, "{");
		for (i=0 ; i<numcomponents ; i++)
			components[i] = MD5_Float (&s);
		MD5_Expect (&s, "}");

		for (i=0 ; i<numjoints ; i++)
		{
			md5joint_t	*jt = &joints[i], *fj = &frame[i];
			float		c[6], q[4];
			vec3_t		p;

			VectorCopy (jt->pos, c);
			VectorCopy (jt->q, (c + 3));
			for (j=k=0 ; j<6 ; j++)
				if (jt->flags & (1<<j))
					c[j] = components[jt->start + k++];
			VectorCopy (c, p);
			VectorCopy ((c + 3), q);
			Quat_W (q);

			if (jt->scale_pos)
				VectorScale (p, scales[fn * numjoints + i], p);

			if (jt->parent < 0)
			{
				VectorCopy (p, fj->pos);
				memcpy (fj->q, q, sizeof(q));
			}
			else
			{
				md5joint_t	*pj = &frame[jt->parent];

				Quat_Rotate (pj->q, p, fj->pos);
				VectorAdd (pj->pos, fj->pos, fj->pos);
				Quat_Mul (pj->q, q, fj->q);
			}
		}

		for (i=0 ; i<numverts ; i++)
		{
			VectorClear (pos[i]);
			for (j=0 ; j<verts[i].count ; j++)
			{
				md5weight_t	*w = &weights[verts[i].start + j];
				md5joint_t	*fj = &frame[w->joint];
				vec3_t		v;

				Quat_Rotate (fj->q, w->pos, v);
				VectorScale (v, scales[fn * numjoints + w->joint], v);
				VectorAdd (v, fj->pos, v);
				VectorMA (pos[i], w->bias, v, pos[i]);
			}
		}

		// a seam's vertices are the same, for their normals
		if (!welded)
		{
			for (i=0 ; i<numverts ; i++)
				order[i] = i;
			md5_sortpos = pos;
			qsort (order, numverts, sizeof(*order), MD5_ComparePos);
			for (i=0 ; i<numverts ; i++)
				weld[order[i]] = i && VectorCompare (pos[order[i]], pos[order[i-1]])
					? weld[order[i-1]] : order[i];
			welded = true;
		}

		memset (nacc, 0, numverts * sizeof(*nacc));
		for (i=0 ; i<numtris ; i++)
		{
			int		*t = &tris[i*3];

			VectorSubtract (pos[t[1]], pos[t[0]], e1);
			VectorSubtract (pos[t[2]], pos[t[0]], e2);
			CrossProduct (e2, e1, n);
			for (j=0 ; j<3 ; j++)
				VectorAdd (nacc[weld[t[j]]], n, nacc[weld[t[j]]]);
		}

		ClearBounds (mins, maxs);
		for (i=0 ; i<numverts ; i++)
			AddPointToBounds (pos[i], mins, maxs);

		fr = (daliasframe_t *)(out + hdr->ofs_frames + fn * framesize);
		if (fn < in.num_frames)
			memcpy (fr->name, (byte *)md2 + in.ofs_frames + fn * in.framesize
				+ offsetof (daliasframe_t, name), sizeof(fr->name));
		else
			Com_sprintf (fr->name, sizeof(fr->name), "md5_%i", fn);
		for (j=0 ; j<3 ; j++)
		{
			scale[j] = (maxs[j] - mins[j]) / 255;
			if (scale[j] <= 0)
				scale[j] = 1.0f / 255;
			fr->scale[j] = scale[j];
			fr->translate[j] = mins[j];
		}
		for (i=0 ; i<numverts ; i++)
		{
			for (j=0 ; j<3 ; j++)
			{
				k = (int)((pos[i][j] - mins[j]) / scale[j] + 0.5f);
				fr->verts[i].v[j] = k < 0 ? 0 : k > 255 ? 255 : k;
			}
			VectorCopy (nacc[weld[i]], n);
			VectorNormalize (n);
			fr->verts[i].lightnormalindex = MD5_NormalIndex (n);
		}
	}

	// the frames the md5 has not, its last
	for (f=numframes ; f<outframes ; f++)
	{
		fr = (daliasframe_t *)(out + hdr->ofs_frames + f * framesize);
		memcpy (fr, out + hdr->ofs_frames + (numframes - 1) * framesize, framesize);
		memcpy (fr->name, (byte *)md2 + in.ofs_frames + f * in.framesize
			+ offsetof (daliasframe_t, name), sizeof(fr->name));
	}

	free (meshbuf);
	free (animbuf);
	free (scales);
	free (pos);
	free (nacc);
	free (weld);
	free (order);
	free (frame);
	MD5_Free ();

	*outlen = len;
	return out;
}
