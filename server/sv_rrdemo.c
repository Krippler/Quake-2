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
sv_rrdemo.c -- the 2023 re-release's demos, played as 1997 ones.

The re-release records its demos in its own network protocol, 2022, which
it has not published. What is here was worked out from the seven demos in
its pak0, every message of which it reads without a byte left over; where a
field's meaning is a guess, it says so.

The demo server sends the client one message of the demo every 100 ms
(SV_SendClientMessages). A re-release demo is 40 frames a second, each a
delta of the one before, with entities and configstrings past 1997's
limits. So rather than pass its messages on, this keeps the state they
build up and every 100 ms writes the client a 1997 frame of it, delta
compressed against the last one it wrote, with the sounds, temporary
entities and prints of the frames in between. The configstrings and
baselines at the start go out a few messages at a time, each within
MAX_MSGLEN.

What it is like, against 1997's:

	serverdata		long protocol, long spawncount, byte demo, byte tick
					rate (40), string gamedir, short playernum, string
					levelname
	configstring	short index (models 62, sounds 8254, images 10302,
					lights 10814, items 11326, player skins 11582,
					general 11838: re-release's game.h), string
	entities		bits as 1997, and a fifth byte past bit 31. Bit 28 makes
					the model numbers shorts, 29 adds the upper 32 bits of
					effects; skin, effects and renderfx with both their
					8 and 16 bits are longs; solid is a long of four bytes
					(x, y, z down, z up + 32). An entity that is solid
					has its coordinates as floats, any other as 1997's
					1/8 units. Angles are floats. The sound is a short,
					its 0x4000 adding a volume byte and its 0x8000 an
					attenuation byte. Bit 30 is one byte, and a fifth bits
					byte of 0xff six: both unknown, and dropped.
	playerinfo		short flags as 1997's, 0x8000 adding a short of more;
					origin, velocity, delta angles and view angles as
					floats; view offset three shorts in 1/16 and a byte of
					view height; kick three shorts; the gun model a short,
					its frame a byte then a byte of which of its offset
					and angles follow, as floats; stats as two halves of
					32, each a long mask then a short for each bit set.
					A further 0x0001 is four bytes after the stats, a
					screen blend.
	frame			as 1997's, at 40 a second
	sound			as 1997's but for a short sound index
	temp entities	as 1997's, two more types
	new				21 a split-screen client byte, 25 damage directions,
					26 a print with its words to look up (cl_loc.c), 27
					fog, 32 a muzzle flash numbered past 255
*/

#include "server.h"

#define RR_MAX_EDICTS	8192
#define RR_BACKUP		64				// re-release frames kept, for deltas
#define RR_MAX_FRAMEENTS	2048		// entities in one of its frames

// its configstrings
#define RR_CS_STATUSBAR		5
#define RR_CS_AIRACCEL		59
#define RR_CS_MODELS		62
#define RR_CS_SOUNDS		(RR_CS_MODELS+8192)
#define RR_CS_IMAGES		(RR_CS_SOUNDS+2048)
#define RR_CS_LIGHTS		(RR_CS_IMAGES+512)
#define RR_CS_SHADOWLIGHTS	(RR_CS_LIGHTS+256)
#define RR_CS_ITEMS			(RR_CS_SHADOWLIGHTS+256)
#define RR_CS_PLAYERSKINS	(RR_CS_ITEMS+256)
#define RR_CS_GENERAL		(RR_CS_PLAYERSKINS+256)
#define RR_CS_END			(RR_CS_GENERAL+512)

typedef struct
{
	int			number;			// 0 when not there
	vec3_t		origin, angles, old_origin;
	int			modelindex[4];
	int			frame;
	unsigned	skinnum, effects, renderfx, solid;
	int			sound, event;
} rrent_t;

typedef struct
{
	int				framenum;		// -1 when the slot is empty
	int				numents;
	rrent_t			*ents;			// [RR_MAX_FRAMEENTS], by number
	player_state_t	ps;				// already in 1997's terms
	float			psorigin[3], psvelocity[3];
	int				stats[64];
	int				areabytes;
	byte			areabits[MAX_MAP_AREAS/8];
} rrframe_t;

typedef struct
{
	qboolean		active;
	int				tickrate;		// its frames a second
	int				playernum;

	rrent_t			*baselines;		// [RR_MAX_EDICTS]
	rrframe_t		frames[RR_BACKUP];
	int				lastframe;		// the newest it has read

	// what has been written to the client
	int				outframe;		// the last frame written, 0 for none
	entity_state_t	*outents;		// [MAX_EDICTS], by number
	entity_state_t	*outbase;		// [MAX_EDICTS], the client's baselines
	byte			*outhave;		// [MAX_EDICTS]
	player_state_t	outps;

	// its model, sound and image numbers to 1997's
	short			modelmap[8192], soundmap[2048], imagemap[512];
	byte			modelused[MAX_MODELS], soundused[MAX_SOUNDS], imageused[MAX_IMAGES];
	char			*skins[MAX_CLIENTS];	// player names, for "##P0"

	// messages written and not yet sent
	byte			*queue;
	int				queuelen, queuepos;
	byte			pending[MAX_MSGLEN];	// what goes with the next frame
	sizebuf_t		pend;
	qboolean		gamestate;		// the configstrings and baselines are out
} rrdemo_t;

static rrdemo_t	rr;

static void RR_Copy (char *dest, const char *src, int size)
{
	strncpy (dest, src, size - 1);
	dest[size - 1] = 0;
}

static void RR_Cat (char *dest, const char *src, int size)
{
	int		len = strlen (dest);

	if (len < size - 1)
		RR_Copy (dest + len, src, size - len);
}

#define RR_QUEUESIZE	(256*1024)
#define RR_MSGMAX		(MAX_MSGLEN - 64)	// within Netchan_Transmit's room

/*
==============================================================================

THE 1997 SIDE

==============================================================================
*/

static void RR_QueueMessage (sizebuf_t *msg)
{
	if (!msg->cursize || msg->overflowed)
		return;
	if (rr.queuelen + 4 + msg->cursize > RR_QUEUESIZE)
	{
		Com_DPrintf ("RR_QueueMessage: queue full\n");
		return;
	}
	*(int *)(rr.queue + rr.queuelen) = msg->cursize;
	memcpy (rr.queue + rr.queuelen + 4, msg->data, msg->cursize);
	rr.queuelen += 4 + msg->cursize;
}

/*
A message for the gamestate, filled as far as it goes then queued and
another begun. write is called again on a fresh buffer when one fills.
*/
static sizebuf_t	rr_gs;
static byte			rr_gsbuf[MAX_MSGLEN];

static void RR_GSBegin (void)
{
	SZ_Init (&rr_gs, rr_gsbuf, RR_MSGMAX);
	rr_gs.allowoverflow = true;
}

static void RR_GSFlush (void)
{
	RR_QueueMessage (&rr_gs);
	RR_GSBegin ();
}

// make room for size more bytes in the gamestate message
static void RR_GSRoom (int size)
{
	if (rr_gs.cursize + size > rr_gs.maxsize)
		RR_GSFlush ();
}

static int RR_MapIndex (short *map, int rrcount, byte *used, int max, int i)
{
	int		j;

	if (i <= 0 || i >= rrcount)
		return 0;
	if (map[i] >= 0)
		return map[i];
	if (i < max - 1 && !used[i])
		j = i;
	else
	{
		// past 1997's limit: the first number nothing else has
		for (j = max - 2 ; j > 0 && used[j] ; j--)
			;
		if (!j)
		{
			map[i] = 0;
			return 0;
		}
	}
	used[j] = 1;
	map[i] = j;
	return j;
}

static int RR_Model (int i)
{
	if (i == 255)
		return 255;		// the player's own model, in both
	return RR_MapIndex (rr.modelmap, 8192, rr.modelused, MAX_MODELS, i);
}

static int RR_Sound (int i)
{
	return RR_MapIndex (rr.soundmap, 2048, rr.soundused, MAX_SOUNDS, i);
}

static int RR_Image (int i)
{
	return RR_MapIndex (rr.imagemap, 512, rr.imageused, MAX_IMAGES, i);
}

/*
Its configstring number in 1997's, or -1 for one 1997 has no place for.
*/
static int RR_ConfigIndex (int i)
{
	if (i >= 0 && i < RR_CS_STATUSBAR)
		return i;
	if (i >= RR_CS_AIRACCEL && i < RR_CS_MODELS)
		return CS_AIRACCEL + (i - RR_CS_AIRACCEL);
	if (i >= RR_CS_MODELS && i < RR_CS_SOUNDS)
	{
		i = RR_Model (i - RR_CS_MODELS);
		return i && i != 255 ? CS_MODELS + i : -1;
	}
	if (i >= RR_CS_SOUNDS && i < RR_CS_IMAGES)
		return (i = RR_Sound (i - RR_CS_SOUNDS)) ? CS_SOUNDS + i : -1;
	if (i >= RR_CS_IMAGES && i < RR_CS_LIGHTS)
		return (i = RR_Image (i - RR_CS_IMAGES)) ? CS_IMAGES + i : -1;
	if (i >= RR_CS_LIGHTS && i < RR_CS_SHADOWLIGHTS)
		return CS_LIGHTS + (i - RR_CS_LIGHTS);
	if (i >= RR_CS_ITEMS && i < RR_CS_PLAYERSKINS)
		return CS_ITEMS + (i - RR_CS_ITEMS);
	if (i >= RR_CS_PLAYERSKINS && i < RR_CS_GENERAL)
		return CS_PLAYERSKINS + (i - RR_CS_PLAYERSKINS);
	if (i >= RR_CS_GENERAL && i < RR_CS_END)
		return CS_GENERAL + (i - RR_CS_GENERAL);
	return -1;
}

static void RR_WriteConfigstring (sizebuf_t *msg, int rrindex, char *s)
{
	int		i;
	char	buf[MAX_QPATH*2], *p;

	i = RR_ConfigIndex (rrindex);
	if (i < 0)
		return;
	if (i >= CS_PLAYERSKINS && i < CS_PLAYERSKINS + MAX_CLIENTS)
	{
		// "name\model/skin\something more": 1997 has no third part
		RR_Copy (buf, s, sizeof(buf));
		if ((p = strchr (buf, '\\')) && (p = strchr (p + 1, '\\')))
			*p = 0;
		s = buf;
		i -= CS_PLAYERSKINS;
		if (rr.skins[i])
			Z_Free (rr.skins[i]);
		rr.skins[i] = CopyString (buf);
		if ((p = strchr (rr.skins[i], '\\')))
			*p = 0;
		i += CS_PLAYERSKINS;
	}
	MSG_WriteByte (msg, svc_configstring);
	MSG_WriteShort (msg, i);
	MSG_WriteString (msg, s);
}

/*
Its entity in 1997's terms: numbers in range, the bits 1997 does not have
taken off, the solid in 1997's packing.
*/
static void RR_ToEntityState (rrent_t *e, entity_state_t *s)
{
	int		i, x, zd, zu;

	memset (s, 0, sizeof(*s));
	s->number = e->number;
	for (i = 0 ; i < 3 ; i++)
	{
		// as the client will have them, so unchanged ones compare equal
		s->origin[i] = (int)(e->origin[i] * 8) * 0.125f;
		s->old_origin[i] = (int)(e->old_origin[i] * 8) * 0.125f;
		s->angles[i] = ((int)(e->angles[i] * 256.0f / 360.0f) & 255) * (360.0f / 256);
	}
	s->modelindex = RR_Model (e->modelindex[0]);
	s->modelindex2 = RR_Model (e->modelindex[1]);
	s->modelindex3 = RR_Model (e->modelindex[2]);
	s->modelindex4 = RR_Model (e->modelindex[3]);
	s->frame = e->frame;
	s->skinnum = e->skinnum & 0xffff;	// the re-release has the view height above
	s->effects = e->effects & ~4;		// 4 is new
	s->renderfx = e->renderfx & 0x79fff;	// 0x2000, 0x4000 and past 0x40000 are new
	s->sound = RR_Sound (e->sound);
	s->event = e->event;
	if (s->event > EV_OTHER_TELEPORT)
		s->event = s->event == 8 ? EV_FOOTSTEP : 0;	// a monster's footstep, a ladder
	if (e->solid == 31)
		s->solid = 31;
	else if (e->solid)
	{
		x = (e->solid & 255) / 8;
		zd = ((e->solid >> 16) & 255) / 8;
		zu = (((e->solid >> 24) & 255)) / 8;
		if (x > 31) x = 31;
		if (zd > 31) zd = 31;
		if (zu > 63) zu = 63;
		if (x < 1) x = 1;
		s->solid = x | (zd << 5) | (zu << 10);
	}
}

/*
The frame of now, written as the client's next: the player's state and
every entity, each against what was written last.
*/
static void RR_WriteFrame (rrframe_t *f)
{
	sizebuf_t		msg;
	byte			buf[MAX_MSGLEN];
	client_frame_t	from, to;
	entity_state_t	news, *olds;
	int				i, j, mark;
	rrent_t			*e;
	rrent_t			*have[MAX_EDICTS];

	SZ_Init (&msg, buf, RR_MSGMAX);
	msg.allowoverflow = true;

	MSG_WriteByte (&msg, svc_frame);
	MSG_WriteLong (&msg, rr.outframe + 1);
	MSG_WriteLong (&msg, rr.outframe ? rr.outframe : -1);
	MSG_WriteByte (&msg, 0);
	MSG_WriteByte (&msg, f->areabytes);
	SZ_Write (&msg, f->areabits, f->areabytes);

	memset (&from, 0, sizeof(from));
	memset (&to, 0, sizeof(to));
	from.ps = rr.outps;
	to.ps = f->ps;
	SV_WritePlayerstateToClient (rr.outframe ? &from : NULL, &to, &msg);

	if (!rr.outframe)
		memset (rr.outhave, 0, MAX_EDICTS);

	// what is there now
	memset (have, 0, sizeof(have));
	for (j = 0 ; j < f->numents ; j++)
		if (f->ents[j].number > 0 && f->ents[j].number < MAX_EDICTS)
			have[f->ents[j].number] = &f->ents[j];

	MSG_WriteByte (&msg, svc_packetentities);
	for (i = 1 ; i < MAX_EDICTS ; i++)
	{
		if (!have[i] && !rr.outhave[i])
			continue;
		mark = msg.cursize;
		if ((e = have[i]))
		{
			RR_ToEntityState (e, &news);
			if (rr.outhave[i])
				olds = &rr.outents[i];
			else
			{
				olds = &rr.outbase[i];
				olds->number = i;
			}
			// one the client has not got goes out whatever, as in
			// SV_EmitPacketEntities
			MSG_WriteDeltaEntity (olds, &news, &msg, !rr.outhave[i], !rr.outhave[i]);
		}
		else
		{
			// gone: a remove
			if (i >= 256)
				MSG_WriteByte (&msg, 0x40 | 0x80), MSG_WriteByte (&msg, 0x01), MSG_WriteShort (&msg, i);
			else
				MSG_WriteByte (&msg, 0x40), MSG_WriteByte (&msg, i);
		}
		if (msg.cursize + 2 > RR_MSGMAX - 2)
		{
			// out of room: the rest wait for the next frame, the client
			// keeping what it has of them
			msg.cursize = mark;
			break;
		}
		if (have[i])
		{
			rr.outents[i] = news;
			rr.outents[i].event = 0;
			rr.outhave[i] = 1;
		}
		else
			rr.outhave[i] = 0;
	}
	MSG_WriteShort (&msg, 0);

	// the sounds, prints and effects of the frames since the last
	if (msg.cursize + rr.pend.cursize <= RR_MSGMAX)
		SZ_Write (&msg, rr.pend.data, rr.pend.cursize);
	else
	{
		RR_QueueMessage (&rr.pend);
	}
	SZ_Clear (&rr.pend);

	rr.outps = f->ps;
	rr.outframe++;
	RR_QueueMessage (&msg);
}

/*
==============================================================================

THE RE-RELEASE'S SIDE

==============================================================================
*/

static rrframe_t *RR_Frame (int framenum)
{
	rrframe_t	*f = &rr.frames[framenum & (RR_BACKUP - 1)];

	return f->framenum == framenum ? f : NULL;
}

/*
An entity's bits, its number and its fields. Returns false at the end of a
packet's entities.
*/
static unsigned long long RR_ReadBits (sizebuf_t *m, int *number)
{
	unsigned long long	bits;
	int		b;

	bits = MSG_ReadByte (m);
	if (bits & 0x80)
		bits |= (unsigned long long)MSG_ReadByte (m) << 8;
	if (bits & 0x8000)
		bits |= (unsigned long long)MSG_ReadByte (m) << 16;
	if (bits & 0x800000)
		bits |= (unsigned long long)MSG_ReadByte (m) << 24;
	if (bits & 0x80000000ULL)
	{
		b = MSG_ReadByte (m);
		bits |= (unsigned long long)b << 32;
	}
	if (bits & U_NUMBER16)
		*number = MSG_ReadShort (m) & 0xffff;
	else
		*number = MSG_ReadByte (m);
	return bits;
}

static void RR_ReadEntity (sizebuf_t *m, unsigned long long bits, rrent_t *e)
{
	int			i, sd;
	qboolean	floats;

#define RR_READMODEL()	((bits & (1<<28)) ? (MSG_ReadShort (m) & 0xffff) : MSG_ReadByte (m))
	if (bits & U_MODEL)
		e->modelindex[0] = RR_READMODEL ();
	if (bits & U_MODEL2)
		e->modelindex[1] = RR_READMODEL ();
	if (bits & U_MODEL3)
		e->modelindex[2] = RR_READMODEL ();
	if (bits & U_MODEL4)
		e->modelindex[3] = RR_READMODEL ();
#undef RR_READMODEL

	if (bits & U_FRAME8)
		e->frame = MSG_ReadByte (m);
	if (bits & U_FRAME16)
		e->frame = MSG_ReadShort (m) & 0xffff;

	if ((bits & U_SKIN8) && (bits & U_SKIN16))
		e->skinnum = MSG_ReadLong (m);
	else if (bits & U_SKIN8)
		e->skinnum = MSG_ReadByte (m);
	else if (bits & U_SKIN16)
		e->skinnum = MSG_ReadShort (m) & 0xffff;

	if ((bits & (U_EFFECTS8|U_EFFECTS16)) == (U_EFFECTS8|U_EFFECTS16))
		e->effects = MSG_ReadLong (m);
	else if (bits & U_EFFECTS8)
		e->effects = MSG_ReadByte (m);
	else if (bits & U_EFFECTS16)
		e->effects = MSG_ReadShort (m) & 0xffff;
	if (bits & (1<<29))
		MSG_ReadLong (m);		// the upper 32 bits of its effects

	if ((bits & (U_RENDERFX8|U_RENDERFX16)) == (U_RENDERFX8|U_RENDERFX16))
		e->renderfx = MSG_ReadLong (m);
	else if (bits & U_RENDERFX8)
		e->renderfx = MSG_ReadByte (m);
	else if (bits & U_RENDERFX16)
		e->renderfx = MSG_ReadShort (m) & 0xffff;

	if (bits & U_SOLID)
		e->solid = MSG_ReadLong (m);

	floats = e->solid != 0;
#define RR_READCOORD()	(floats ? MSG_ReadFloat (m) : MSG_ReadShort (m) * 0.125f)
	if (bits & U_ORIGIN1)
		e->origin[0] = RR_READCOORD ();
	if (bits & U_ORIGIN2)
		e->origin[1] = RR_READCOORD ();
	if (bits & U_ORIGIN3)
		e->origin[2] = RR_READCOORD ();
	if (bits & U_OLDORIGIN)
		for (i = 0 ; i < 3 ; i++)
			e->old_origin[i] = RR_READCOORD ();
#undef RR_READCOORD

	if (bits & U_ANGLE1)
		e->angles[0] = MSG_ReadFloat (m);
	if (bits & U_ANGLE2)
		e->angles[1] = MSG_ReadFloat (m);
	if (bits & U_ANGLE3)
		e->angles[2] = MSG_ReadFloat (m);

	if (bits & U_SOUND)
	{
		sd = MSG_ReadShort (m) & 0xffff;
		e->sound = sd & 0x3fff;
		if (sd & 0x4000)
			MSG_ReadByte (m);	// its volume
		if (sd & 0x8000)
			MSG_ReadByte (m);	// its attenuation
	}

	e->event = (bits & U_EVENT) ? MSG_ReadByte (m) : 0;

	if (bits & (1<<30))
		MSG_ReadByte (m);
	if (bits >> 32)
		m->readcount += 6;
}

/*
The player's state, against the frame's it is a delta of.
*/
static void RR_ReadPlayerstate (sizebuf_t *m, rrframe_t *from, rrframe_t *f)
{
	int		flags, ext, i, gb, half, mask;
	float	v;
	static const int	pmtypes[] = { PM_NORMAL, PM_NORMAL, PM_SPECTATOR,
		PM_SPECTATOR, PM_DEAD, PM_GIB, PM_FREEZE };
	player_state_t	*ps = &f->ps;
	vec3_t	view;
	int		viewheight;

	if (from)
	{
		f->ps = from->ps;
		VectorCopy (from->psorigin, f->psorigin);
		VectorCopy (from->psvelocity, f->psvelocity);
		memcpy (f->stats, from->stats, sizeof(f->stats));
	}
	else
	{
		memset (&f->ps, 0, sizeof(f->ps));
		VectorClear (f->psorigin);
		VectorClear (f->psvelocity);
		memset (f->stats, 0, sizeof(f->stats));
		f->ps.fov = 90;
		f->ps.viewoffset[2] = 22;
	}
	ps->pmove.pm_flags &= ~PMF_NO_PREDICTION;

	flags = MSG_ReadShort (m) & 0xffff;
	ext = (flags & 0x8000) ? MSG_ReadShort (m) & 0xffff : 0;

	if (flags & PS_M_TYPE)
	{
		i = MSG_ReadByte (m);
		ps->pmove.pm_type = i < 7 ? pmtypes[i] : PM_NORMAL;
	}
	if (flags & PS_M_ORIGIN)
		for (i = 0 ; i < 3 ; i++)
			f->psorigin[i] = MSG_ReadFloat (m);
	if (flags & PS_M_VELOCITY)
		for (i = 0 ; i < 3 ; i++)
			f->psvelocity[i] = MSG_ReadFloat (m);
	if (flags & PS_M_TIME)
		ps->pmove.pm_time = MSG_ReadByte (m);
	if (flags & PS_M_FLAGS)
		ps->pmove.pm_flags = MSG_ReadShort (m) & 0x3f;
	if (flags & PS_M_GRAVITY)
		ps->pmove.gravity = MSG_ReadShort (m);
	if (flags & PS_M_DELTA_ANGLES)
		for (i = 0 ; i < 3 ; i++)
			ps->pmove.delta_angles[i] = ANGLE2SHORT (MSG_ReadFloat (m));

	if (flags & PS_VIEWOFFSET)
	{
		for (i = 0 ; i < 3 ; i++)
			view[i] = MSG_ReadShort (m) / 16.0f;
		viewheight = (signed char)MSG_ReadByte (m);
		ps->viewoffset[0] = view[0];
		ps->viewoffset[1] = view[1];
		ps->viewoffset[2] = view[2] + viewheight;
	}
	if (flags & PS_VIEWANGLES)
		for (i = 0 ; i < 3 ; i++)
			ps->viewangles[i] = MSG_ReadFloat (m);
	if (flags & PS_KICKANGLES)
		for (i = 0 ; i < 3 ; i++)
			ps->kick_angles[i] = MSG_ReadShort (m) / 1024.0f;	// a guess of scale
	if (flags & PS_WEAPONINDEX)
		ps->gunindex = RR_Model (MSG_ReadShort (m) & 0xffff);
	if (flags & PS_WEAPONFRAME)
	{
		ps->gunframe = MSG_ReadByte (m);
		gb = MSG_ReadByte (m);
		for (i = 0 ; i < 7 ; i++)
			if (gb & (1 << i))
			{
				v = MSG_ReadFloat (m);
				if (i < 3)
					ps->gunoffset[i] = v;
				else if (i >= 4)
					ps->gunangles[i - 4] = v;
			}
		if (gb & 0x80)
			MSG_ReadByte (m);	// its rate?
	}
	if (flags & PS_BLEND)
		for (i = 0 ; i < 4 ; i++)
			ps->blend[i] = MSG_ReadByte (m) / 255.0f;
	if (flags & PS_FOV)
		ps->fov = MSG_ReadByte (m);
	if (flags & PS_RDFLAGS)
		ps->rdflags = MSG_ReadByte (m);

	for (half = 0 ; half < 64 ; half += 32)
	{
		mask = MSG_ReadLong (m);
		for (i = 0 ; i < 32 ; i++)
			if (mask & (1 << i))
				f->stats[half + i] = MSG_ReadShort (m);
	}
	if (ext & 1)
	{
		// a screen blend of its own, a flash of damage
		for (i = 0 ; i < 4 ; i++)
			MSG_ReadByte (m);
	}

	for (i = 0 ; i < 3 ; i++)
	{
		ps->pmove.origin[i] = (int)(f->psorigin[i] * 8);
		ps->pmove.velocity[i] = (int)(f->psvelocity[i] * 8);
	}

	// the stats 1997 has, its numbers made 1997's
	for (i = 0 ; i < MAX_STATS ; i++)
		ps->stats[i] = f->stats[i];
	ps->stats[STAT_HEALTH_ICON] = RR_Image (f->stats[STAT_HEALTH_ICON]);
	ps->stats[STAT_AMMO_ICON] = RR_Image (f->stats[STAT_AMMO_ICON]);
	ps->stats[STAT_ARMOR_ICON] = RR_Image (f->stats[STAT_ARMOR_ICON]);
	ps->stats[STAT_SELECTED_ICON] = RR_Image (f->stats[STAT_SELECTED_ICON]);
	ps->stats[STAT_PICKUP_ICON] = RR_Image (f->stats[STAT_PICKUP_ICON]);
	ps->stats[STAT_TIMER_ICON] = RR_Image (f->stats[STAT_TIMER_ICON]);
	ps->stats[STAT_HELPICON] = RR_Image (f->stats[STAT_HELPICON]);
	i = f->stats[STAT_PICKUP_STRING] ? RR_ConfigIndex (f->stats[STAT_PICKUP_STRING] & 0xffff) : 0;
	ps->stats[STAT_PICKUP_STRING] = i > 0 ? i : 0;
}

static qboolean RR_ReadPacketEntities (sizebuf_t *m, rrframe_t *from, rrframe_t *f)
{
	int			number, i;
	unsigned long long	bits;
	rrent_t		*olde, cur;
	static rrent_t	work[RR_MAX_EDICTS];
	static byte		inwork[RR_MAX_EDICTS];

	// what there was: the frame this is a delta of
	memset (inwork, 0, sizeof(inwork));
	if (from)
		for (i = 0 ; i < from->numents ; i++)
		{
			olde = &from->ents[i];
			work[olde->number] = *olde;
			work[olde->number].event = 0;
			inwork[olde->number] = 1;
		}

	while (1)
	{
		bits = RR_ReadBits (m, &number);
		if (m->readcount > m->cursize || number >= RR_MAX_EDICTS)
			return false;
		if (!number && !bits)
			break;
		if (bits & U_REMOVE)
		{
			inwork[number] = 0;
			continue;
		}
		if (inwork[number])
			cur = work[number];
		else
			cur = rr.baselines[number];
		cur.number = number;
		RR_ReadEntity (m, bits, &cur);
		work[number] = cur;
		inwork[number] = 1;
	}

	f->numents = 0;
	for (i = 1 ; i < RR_MAX_EDICTS && f->numents < RR_MAX_FRAMEENTS ; i++)
		if (inwork[i])
			f->ents[f->numents++] = work[i];
	return m->readcount <= m->cursize;
}

static void RR_ReadSound (sizebuf_t *m)
{
	int		flags, index, vol = -1, atten = -1, ofs = -1, ent = -1, i;
	short	pos[3];
	sizebuf_t	*o = &rr.pend;

	flags = MSG_ReadByte (m);
	index = MSG_ReadShort (m) & 0xffff;
	if (flags & SND_VOLUME)
		vol = MSG_ReadByte (m);
	if (flags & SND_ATTENUATION)
		atten = MSG_ReadByte (m);
	if (flags & SND_OFFSET)
		ofs = MSG_ReadByte (m);
	if (flags & SND_ENT)
		ent = MSG_ReadShort (m) & 0xffff;
	if (flags & SND_POS)
		for (i = 0 ; i < 3 ; i++)
			pos[i] = MSG_ReadShort (m);

	index = RR_Sound (index);
	if (!index)
		return;
	flags &= SND_VOLUME|SND_ATTENUATION|SND_POS|SND_ENT|SND_OFFSET;
	if ((flags & SND_ENT) && (ent >> 3) >= MAX_EDICTS)
	{
		flags &= ~SND_ENT;		// heard where it is, if it says
		if (!(flags & SND_POS))
			return;
	}
	if (o->cursize + 16 > o->maxsize)
		return;
	MSG_WriteByte (o, svc_sound);
	MSG_WriteByte (o, flags);
	MSG_WriteByte (o, index);
	if (flags & SND_VOLUME)
		MSG_WriteByte (o, vol);
	if (flags & SND_ATTENUATION)
		MSG_WriteByte (o, atten);
	if (flags & SND_OFFSET)
		MSG_WriteByte (o, ofs);
	if (flags & SND_ENT)
		MSG_WriteShort (o, ent);
	if (flags & SND_POS)
		for (i = 0 ; i < 3 ; i++)
			MSG_WriteShort (o, pos[i]);
}

/*
Temporary entities are as 1997's, a byte of type then positions in 1/8
units and directions as bytes; the layout of each type, in cl_tent.c's
order: P a position, D a direction, B a byte, S a short.
*/
static const char *rr_tents[] =
{
	"PD", "PD", "PD", "PP", "PD", "P", "P", "P", "P", "PD",		// 0
	"BPDB", "PP", "PD", "PD", "PD", "BPDB", "SPP", "P", "P", "SPP",	// 10
	"P", "P", "P", "PP", "SPPP", "BPDB", "PD", "PP", "P", "BPDB",	// 20
	"PD", NULL, NULL, "SSPP", "PP", "P", "PS", "PPB", "SPP", "SPP",	// 30
	NULL, "PP", "PD", "PD", "PD", "P", "PD", "P", "P", "P",		// 40
	"SP", "P", "P", "P", "P", "PD", "PD", NULL, "PD"			// 50
};

static qboolean RR_ReadTempEntity (sizebuf_t *m)
{
	int			type, start, size, id, out, ent;
	const char	*p;
	sizebuf_t	*o = &rr.pend;

	start = m->readcount;
	type = MSG_ReadByte (m);
	out = type;
	if (type == TE_STEAM)
	{
		id = MSG_ReadShort (m);
		size = 2 + 1 + 6 + 1 + 1 + 2 + (id != -1 ? 4 : 0);
	}
	else
	{
		if (type >= (int)(sizeof(rr_tents)/sizeof(rr_tents[0])) || !(p = rr_tents[type]))
			return false;
		size = 0;
		for ( ; *p ; p++)
			size += *p == 'P' ? 6 : *p == 'S' ? 2 : 1;
	}
	m->readcount = start + 1 + size;
	if (m->readcount > m->cursize)
		return false;

	if (type == 56)
		out = TE_BLASTER;		// its blue blaster's, as the hyperblaster's
	else if (type > TE_FLECHETTE)
		return true;			// something of its own: left out
	// a beam from an entity 1997 has no number for is left out
	p = rr_tents[type];
	if (p && p[0] == 'S')
	{
		ent = m->data[start + 1] | (m->data[start + 2] << 8);
		if (ent >= MAX_EDICTS)
			return true;
	}
	if (o->cursize + 1 + size > o->maxsize)
		return true;
	MSG_WriteByte (o, svc_temp_entity);
	MSG_WriteByte (o, out);
	SZ_Write (o, m->data + start + 1, size);
	return true;
}

/*
"$key", then its arguments, each "\x1f"-separated, for CL_Localize to put
in the key's "{}"; "##P0" is player 0's name.
*/
static void RR_ReadLocPrint (sizebuf_t *m)
{
	int		level, n, i, cl;
	char	text[1024], *s;
	sizebuf_t	*o = &rr.pend;

	level = MSG_ReadByte (m);
	RR_Copy (text, MSG_ReadString (m), sizeof(text));
	n = MSG_ReadByte (m);
	for (i = 0 ; i < n ; i++)
	{
		s = MSG_ReadString (m);
		if (!strncmp (s, "##P", 3))
		{
			cl = atoi (s + 3);
			s = (cl >= 0 && cl < MAX_CLIENTS && rr.skins[cl]) ? rr.skins[cl] : "player";
		}
		RR_Cat (text, "\x1f", sizeof(text));
		RR_Cat (text, s, sizeof(text));
	}
	if (o->cursize + strlen (text) + 4 > o->maxsize)
		return;
	if ((level & 7) == 4 || (level & 7) == 5)
	{
		// the typewriter's and the centre's
		MSG_WriteByte (o, svc_centerprint);
		MSG_WriteString (o, text);
	}
	else
	{
		RR_Cat (text, "\n", sizeof(text));
		MSG_WriteByte (o, svc_print);
		MSG_WriteByte (o, (level & 7) > PRINT_CHAT ? PRINT_HIGH : (level & 7));
		MSG_WriteString (o, text);
	}
}

/*
The single player's status bar of 1997's game, for the re-release's
own, which draws with commands a 1997 client does not have.
*/
static char *rr_statusbar =
	"yb -24 xv 0 hnum xv 50 pic 0 "
	"if 2 xv 100 anum xv 150 pic 2 endif "
	"if 4 xv 200 rnum xv 250 pic 4 endif "
	"if 6 xv 296 pic 6 endif "
	"yb -50 "
	"if 7 xv 0 pic 7 xv 26 yb -42 stat_string 8 yb -50 endif "
	"if 9 xv 262 num 2 10 xv 296 pic 9 endif "
	"if 11 xv 148 pic 11 endif ";

/*
One message of the demo. Returns false when it cannot be read: the end of
it, for this.
*/
static qboolean RR_ReadMessage (sizebuf_t *m, qboolean *framed)
{
	int			cmd, i, n, number, fn, dn;
	unsigned long long	bits;
	char		*s;
	rrframe_t	*f, *from;
	rrent_t		e;

	while (m->readcount < m->cursize)
	{
		cmd = MSG_ReadByte (m);
		switch (cmd)
		{
		case svc_serverdata:
			MSG_ReadLong (m);		// protocol
			MSG_ReadLong (m);		// spawncount
			MSG_ReadByte (m);		// demo
			rr.tickrate = MSG_ReadByte (m);
			s = MSG_ReadString (m);
			RR_GSRoom (32 + MAX_QPATH*2);
			MSG_WriteByte (&rr_gs, svc_serverdata);
			MSG_WriteLong (&rr_gs, PROTOCOL_VERSION);
			MSG_WriteLong (&rr_gs, svs.spawncount);
			MSG_WriteByte (&rr_gs, 1);		// attract loop
			MSG_WriteString (&rr_gs, s);
			rr.playernum = MSG_ReadShort (m);
			MSG_WriteShort (&rr_gs, rr.playernum);
			MSG_WriteString (&rr_gs, MSG_ReadString (m));
			RR_GSRoom (strlen (rr_statusbar) + 8);
			MSG_WriteByte (&rr_gs, svc_configstring);
			MSG_WriteShort (&rr_gs, CS_STATUSBAR);
			MSG_WriteString (&rr_gs, rr_statusbar);
			break;

		case svc_configstring:
			i = MSG_ReadShort (m) & 0xffff;
			s = MSG_ReadString (m);
			if (i >= RR_CS_STATUSBAR && i < RR_CS_AIRACCEL)
				break;
			if (rr.gamestate)
				RR_WriteConfigstring (&rr.pend, i, s);
			else
			{
				RR_GSRoom (strlen (s) + 8);
				RR_WriteConfigstring (&rr_gs, i, s);
			}
			break;

		case svc_spawnbaseline:
			bits = RR_ReadBits (m, &number);
			if (number <= 0 || number >= RR_MAX_EDICTS)
				return false;
			memset (&e, 0, sizeof(e));
			e.number = number;
			RR_ReadEntity (m, bits, &e);
			e.event = 0;
			rr.baselines[number] = e;
			if (number < MAX_EDICTS && !rr.gamestate)
			{
				entity_state_t	base;
				static entity_state_t	none;

				RR_ToEntityState (&e, &base);
				rr.outbase[number] = base;
				RR_GSRoom (64);
				MSG_WriteByte (&rr_gs, svc_spawnbaseline);
				MSG_WriteDeltaEntity (&none, &base, &rr_gs, true, true);
			}
			break;

		case svc_stufftext:
			s = MSG_ReadString (m);
			if (!rr.gamestate && !strcmp (s, "precache\n"))
			{
				// the end of the gamestate
				RR_GSRoom (16);
				MSG_WriteByte (&rr_gs, svc_stufftext);
				MSG_WriteString (&rr_gs, "precache\n");
				RR_GSFlush ();
				rr.gamestate = true;
			}
			break;

		case svc_frame:
			fn = MSG_ReadLong (m);
			dn = MSG_ReadLong (m);
			MSG_ReadByte (m);
			f = &rr.frames[fn & (RR_BACKUP - 1)];
			f->framenum = -1;
			f->areabytes = MSG_ReadByte (m);
			if (f->areabytes > (int)sizeof(f->areabits))
				return false;
			MSG_ReadData (m, f->areabits, f->areabytes);
			from = dn > 0 ? RR_Frame (dn) : NULL;
			if (dn > 0 && !from)
				return false;		// a delta of a frame it has not got
			if (MSG_ReadByte (m) != svc_playerinfo)
				return false;
			RR_ReadPlayerstate (m, from, f);
			if (MSG_ReadByte (m) != svc_packetentities)
				return false;
			if (!RR_ReadPacketEntities (m, from, f))
				return false;
			f->framenum = fn;
			rr.lastframe = fn;
			*framed = true;
			break;

		case svc_sound:
			RR_ReadSound (m);
			break;

		case svc_temp_entity:
			if (!RR_ReadTempEntity (m))
				return false;
			break;

		case svc_muzzleflash:
		case svc_muzzleflash2:
			n = MSG_ReadShort (m) & 0xffff;
			i = MSG_ReadByte (m);
			if (cmd == svc_muzzleflash2 && i > MZ2_SOLDIER_HYPERGUN_9)
				break;		// a monster's flash of its own
			if (n < MAX_EDICTS && rr.pend.cursize + 4 <= rr.pend.maxsize)
			{
				MSG_WriteByte (&rr.pend, cmd);
				MSG_WriteShort (&rr.pend, n);
				MSG_WriteByte (&rr.pend, i);
			}
			break;

		case 32:	// a monster's muzzle flash past 255
			n = MSG_ReadShort (m) & 0xffff;
			i = MSG_ReadShort (m) & 0xffff;
			if (n < MAX_EDICTS && i <= MZ2_MEDIC_HYPERBLASTER2_12 && rr.pend.cursize + 5 <= rr.pend.maxsize)
			{
				// the same numbers as here, all of them now
				MSG_WriteByte (&rr.pend, i > 255 ? svc_muzzleflash3 : svc_muzzleflash2);
				MSG_WriteShort (&rr.pend, n);
				if (i > 255)
					MSG_WriteShort (&rr.pend, i);
				else
					MSG_WriteByte (&rr.pend, i);
			}
			break;

		case svc_print:
			i = MSG_ReadByte (m);
			s = MSG_ReadString (m);
			if (rr.pend.cursize + strlen (s) + 3 <= rr.pend.maxsize)
			{
				MSG_WriteByte (&rr.pend, svc_print);
				MSG_WriteByte (&rr.pend, i);
				MSG_WriteString (&rr.pend, s);
			}
			break;

		case svc_centerprint:
		case svc_layout:
			s = MSG_ReadString (m);
			if (rr.pend.cursize + strlen (s) + 2 <= rr.pend.maxsize)
			{
				MSG_WriteByte (&rr.pend, cmd);
				MSG_WriteString (&rr.pend, s);
			}
			break;

		case svc_inventory:
			for (i = 0 ; i < MAX_ITEMS ; i++)
				MSG_ReadShort (m);
			break;

		case 21:	// which split-screen player the next is for
			MSG_ReadByte (m);
			break;

		case 25:	// directions damage came from
			n = MSG_ReadByte (m);
			m->readcount += 2 * n;
			break;

		case 26:
			RR_ReadLocPrint (m);
			break;

		case 27:	// fog
			n = MSG_ReadByte (m);
			if (n & 0x80)
				n |= MSG_ReadByte (m) << 8;
			if (n & 1)		m->readcount += 5;	// density, sky factor
			if (n & 2)		m->readcount += 1;	// r
			if (n & 4)		m->readcount += 1;	// g
			if (n & 8)		m->readcount += 1;	// b
			if (n & 16)		m->readcount += 2;	// time
			if (n & 32)		m->readcount += 4;	// height fog falloff
			if (n & 64)		m->readcount += 4;	// and density
			if (n & 0x100)	m->readcount += 1;
			if (n & 0x200)	m->readcount += 1;
			if (n & 0x400)	m->readcount += 1;
			if (n & 0x800)	m->readcount += 4;
			if (n & 0x1000)	m->readcount += 1;
			if (n & 0x2000)	m->readcount += 1;
			if (n & 0x4000)	m->readcount += 1;
			if (n & 0x8000)	m->readcount += 4;
			break;

		case svc_nop:
			break;

		case svc_disconnect:
			return false;

		default:
			Com_DPrintf ("re-release demo: message %i not known; ending it\n", cmd);
			return false;
		}
		if (m->readcount > m->cursize)
			return false;
	}
	return true;
}

/*
Reads the demo's next block. False at its end.
*/
static qboolean RR_ReadBlock (qboolean *framed)
{
	int			len;
	static byte	buf[0x20000];
	sizebuf_t	m;

	if (fread (&len, 4, 1, sv.demofile) != 1)
		return false;
	len = LittleLong (len);
	if (len < 0 || len > (int)sizeof(buf))
		return false;
	if (fread (buf, len, 1, sv.demofile) != 1)
		return false;
	SZ_Init (&m, buf, sizeof(buf));
	m.cursize = len;
	return RR_ReadMessage (&m, framed);
}

/*
==============================================================================

THE DEMO SERVER'S

==============================================================================
*/

/*
A demo the re-release recorded: protocol 2022 or 2023.
*/
qboolean SV_RRDemo_IsOne (int protocol)
{
	return protocol == 2022 || protocol == 2023;
}

void SV_RRDemo_Close (void)
{
	int		i;

	if (!rr.active)
		return;
	for (i = 0 ; i < RR_BACKUP ; i++)
		Z_Free (rr.frames[i].ents);
	Z_Free (rr.baselines);
	Z_Free (rr.outents);
	Z_Free (rr.outbase);
	Z_Free (rr.outhave);
	Z_Free (rr.queue);
	for (i = 0 ; i < MAX_CLIENTS ; i++)
		if (rr.skins[i])
			Z_Free (rr.skins[i]);
	memset (&rr, 0, sizeof(rr));
}

/*
sv.demofile is at the start of a re-release demo.
*/
void SV_RRDemo_Open (void)
{
	int		i;

	SV_RRDemo_Close ();
	rr.active = true;
	rr.tickrate = 40;
	rr.baselines = Z_Malloc (RR_MAX_EDICTS * sizeof(rrent_t));
	for (i = 0 ; i < RR_BACKUP ; i++)
	{
		rr.frames[i].framenum = -1;
		rr.frames[i].ents = Z_Malloc (RR_MAX_FRAMEENTS * sizeof(rrent_t));
	}
	rr.outents = Z_Malloc (MAX_EDICTS * sizeof(entity_state_t));
	rr.outbase = Z_Malloc (MAX_EDICTS * sizeof(entity_state_t));
	rr.outhave = Z_Malloc (MAX_EDICTS);
	rr.queue = Z_Malloc (RR_QUEUESIZE);
	memset (rr.modelmap, -1, sizeof(rr.modelmap));
	memset (rr.soundmap, -1, sizeof(rr.soundmap));
	memset (rr.imagemap, -1, sizeof(rr.imagemap));
	SZ_Init (&rr.pend, rr.pending, RR_MSGMAX);
	rr.pend.allowoverflow = true;
	RR_GSBegin ();
}

qboolean SV_RRDemo_Active (void)
{
	return rr.active;
}

/*
The next message for the client, into buf: the length, 0 for none this
frame, -1 at the end of the demo.
*/
int SV_RRDemo_Read (byte *buf, int size)
{
	int			len, frames;
	qboolean	framed;

	while (rr.queuepos >= rr.queuelen)
	{
		rr.queuepos = rr.queuelen = 0;
		if (!rr.gamestate)
		{
			framed = false;
			if (!RR_ReadBlock (&framed))
				return -1;
			continue;
		}

		// 100 ms of it: its frames for that, then one of 1997's
		frames = rr.tickrate / 10;
		if (frames < 1)
			frames = 1;
		while (frames)
		{
			framed = false;
			if (!RR_ReadBlock (&framed))
				return -1;
			if (framed)
				frames--;
		}
		RR_WriteFrame (&rr.frames[rr.lastframe & (RR_BACKUP - 1)]);
	}

	len = *(int *)(rr.queue + rr.queuepos);
	if (len > size)
		return -1;
	memcpy (buf, rr.queue + rr.queuepos + 4, len);
	rr.queuepos += 4 + len;
	return len;
}
