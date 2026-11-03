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
cl_ogv.c -- the 2023 re-release's cinematics: Ogg Theora video, Vorbis sound

The re-release asks for its cinematics by the 1997 names (idlog.cin,
ntro.cin, ...) and has them as video/<name>.ogv. SCR_PlayCinematic plays
the .cin if there is one, and this if not.

Each frame is decoded (libtheora), made no wider than OGV_MAX_WIDTH, and
taken to a palette of its own, the 252 colours of a 6x7x6 cube, with an
ordered dither: the software renderer draws 8-bit pictures, as it draws the
1997 cinematics. The sound (libvorbis) goes out by S_RawSamples, as the
1997 cinematics' does, kept a quarter of a second ahead of what is playing.

Without libtheora (the Makefile's THEORA=no) there are no .ogv cinematics,
and those cinematics are passed over as missing ones are.
*/

#include "client.h"
#include "snd_loc.h"

#ifdef USE_THEORA

#include <ogg/ogg.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>

#define OGV_MAX_WIDTH	640
#define OGV_CUBE		252		// colours in the cube; the rest black

typedef struct
{
	FILE			*f;
	int				remaining;		// bytes of the file left to read

	ogg_sync_state	oy;
	ogg_stream_state	to, vo;
	int				theora_p, vorbis_p;		// header packets read, or 0: none
	th_info			ti;
	th_comment		tc;
	th_setup_info	*ts;
	th_dec_ctx		*td;
	vorbis_info		vi;
	vorbis_comment	vc;
	vorbis_dsp_state	vd;
	vorbis_block	vb;
	qboolean		vorbis_ready;

	double			frame_time;		// the decoded frame's time, seconds
	qboolean		frame_ready;	// a frame decoded and not yet shown
	qboolean		video_done;

	byte			*pic;
	int				width, height, step;
} ogv_t;

static ogv_t	ogv;
static qboolean	ogv_open;

static const int bayer[4][4] =
{
	{  0,  8,  2, 10 },
	{ 12,  4, 14,  6 },
	{  3, 11,  1,  9 },
	{ 15,  7, 13,  5 }
};

// more of the file into the sync layer; false at its end
static qboolean OGV_ReadData (void)
{
	char	*buf;
	int		n;

	if (ogv.remaining <= 0)
		return false;
	n = ogv.remaining < 8192 ? ogv.remaining : 8192;
	buf = ogg_sync_buffer (&ogv.oy, n);
	n = fread (buf, 1, n, ogv.f);
	if (n <= 0)
	{
		ogv.remaining = 0;
		return false;
	}
	ogv.remaining -= n;
	ogg_sync_wrote (&ogv.oy, n);
	return true;
}

// the pages there are to the streams they are of
static void OGV_QueuePages (void)
{
	ogg_page	og;

	while (ogg_sync_pageout (&ogv.oy, &og) > 0)
	{
		if (ogv.theora_p)
			ogg_stream_pagein (&ogv.to, &og);
		if (ogv.vorbis_p)
			ogg_stream_pagein (&ogv.vo, &og);
	}
}

/*
==================
OGV_ReadHeaders

The streams' first pages say what they are; Theora's and Vorbis's three
header packets each follow
==================
*/
static qboolean OGV_ReadHeaders (void)
{
	ogg_page		og;
	ogg_packet		op;
	ogg_stream_state	test;
	qboolean		bos = true;
	int				r;

	th_info_init (&ogv.ti);
	th_comment_init (&ogv.tc);
	vorbis_info_init (&ogv.vi);
	vorbis_comment_init (&ogv.vc);

	// the beginning of each stream
	while (bos)
	{
		if (!OGV_ReadData ())
			return false;
		while (ogg_sync_pageout (&ogv.oy, &og) > 0)
		{
			if (!ogg_page_bos (&og))
			{	// past them: this page is a stream's own
				if (ogv.theora_p)
					ogg_stream_pagein (&ogv.to, &og);
				if (ogv.vorbis_p)
					ogg_stream_pagein (&ogv.vo, &og);
				bos = false;
				break;
			}
			ogg_stream_init (&test, ogg_page_serialno (&og));
			ogg_stream_pagein (&test, &og);
			if (ogg_stream_packetout (&test, &op) != 1)
			{
				ogg_stream_clear (&test);
				continue;
			}
			if (!ogv.theora_p && th_decode_headerin (&ogv.ti, &ogv.tc, &ogv.ts, &op) >= 0)
			{
				memcpy (&ogv.to, &test, sizeof(test));
				ogv.theora_p = 1;
			}
			else if (!ogv.vorbis_p && vorbis_synthesis_headerin (&ogv.vi, &ogv.vc, &op) >= 0)
			{
				memcpy (&ogv.vo, &test, sizeof(test));
				ogv.vorbis_p = 1;
			}
			else
				ogg_stream_clear (&test);
		}
	}
	if (!ogv.theora_p)
		return false;

	// the rest of the headers
	while ((ogv.theora_p && ogv.theora_p < 3) || (ogv.vorbis_p && ogv.vorbis_p < 3))
	{
		while (ogv.theora_p && ogv.theora_p < 3 && (r = ogg_stream_packetout (&ogv.to, &op)))
		{
			if (r < 0 || !th_decode_headerin (&ogv.ti, &ogv.tc, &ogv.ts, &op))
				return false;	// 0: a data packet before the headers' end
			ogv.theora_p++;
		}
		while (ogv.vorbis_p && ogv.vorbis_p < 3 && (r = ogg_stream_packetout (&ogv.vo, &op)))
		{
			if (r < 0 || vorbis_synthesis_headerin (&ogv.vi, &ogv.vc, &op))
				return false;
			ogv.vorbis_p++;
		}
		if ((ogv.theora_p && ogv.theora_p < 3) || (ogv.vorbis_p && ogv.vorbis_p < 3))
		{
			if (ogg_sync_pageout (&ogv.oy, &og) > 0)
			{
				if (ogv.theora_p)
					ogg_stream_pagein (&ogv.to, &og);
				if (ogv.vorbis_p)
					ogg_stream_pagein (&ogv.vo, &og);
			}
			else if (!OGV_ReadData ())
				return false;
		}
	}
	return true;
}

/*
==================
OGV_Open

video/<name>.ogv, if there is one. Its picture is width x height of
palette (OGV_CUBE colours of a 6x7x6 cube)
==================
*/
qboolean OGV_Open (char *name, int *width, int *height, byte *palette)
{
	int		len, i, r, g, b;

	OGV_Close ();
	memset (&ogv, 0, sizeof(ogv));
	len = FS_FOpenFile (name, &ogv.f);
	if (!ogv.f)
		return false;
	ogv.remaining = len;
	ogg_sync_init (&ogv.oy);
	ogv_open = true;

	if (!OGV_ReadHeaders ())
	{
		Com_Printf ("%s: not Ogg Theora, or damaged\n", name);
		OGV_Close ();
		return false;
	}

	ogv.td = th_decode_alloc (&ogv.ti, ogv.ts);
	if (!ogv.td)
	{
		Com_Printf ("%s: its Theora could not be decoded\n", name);
		OGV_Close ();
		return false;
	}
	if (ogv.vorbis_p)
	{
		vorbis_synthesis_init (&ogv.vd, &ogv.vi);
		vorbis_block_init (&ogv.vd, &ogv.vb);
		ogv.vorbis_ready = true;
	}

	// as wide as the picture, or a whole fraction of it no wider than 640
	ogv.step = (ogv.ti.pic_width + OGV_MAX_WIDTH - 1) / OGV_MAX_WIDTH;
	if (ogv.step < 1)
		ogv.step = 1;
	ogv.width = ogv.ti.pic_width / ogv.step;
	ogv.height = ogv.ti.pic_height / ogv.step;
	ogv.pic = Z_Malloc (ogv.width * ogv.height);
	*width = ogv.width;
	*height = ogv.height;

	// the cube: red 6, green 7, blue 6 levels
	for (i = 0 ; i < 256 ; i++)
	{
		if (i < OGV_CUBE)
		{
			r = i / 42;
			g = (i / 6) % 7;
			b = i % 6;
			palette[i*3+0] = r * 255 / 5;
			palette[i*3+1] = g * 255 / 6;
			palette[i*3+2] = b * 255 / 5;
		}
		else
			palette[i*3+0] = palette[i*3+1] = palette[i*3+2] = 0;
	}

	Com_DPrintf ("%s: %ix%i at %.2f fps, shown %ix%i%s\n", name,
		ogv.ti.pic_width, ogv.ti.pic_height,
		ogv.ti.fps_denominator ? (float)ogv.ti.fps_numerator / ogv.ti.fps_denominator : 0,
		ogv.width, ogv.height, ogv.vorbis_p ? ", with sound" : "");
	return true;
}

void OGV_Close (void)
{
	if (!ogv_open)
		return;
	if (ogv.vorbis_ready)
	{
		vorbis_block_clear (&ogv.vb);
		vorbis_dsp_clear (&ogv.vd);
	}
	if (ogv.td)
		th_decode_free (ogv.td);
	if (ogv.ts)
		th_setup_free (ogv.ts);
	if (ogv.theora_p)
		ogg_stream_clear (&ogv.to);
	if (ogv.vorbis_p)
		ogg_stream_clear (&ogv.vo);
	th_info_clear (&ogv.ti);
	th_comment_clear (&ogv.tc);
	vorbis_comment_clear (&ogv.vc);
	vorbis_info_clear (&ogv.vi);
	ogg_sync_clear (&ogv.oy);
	if (ogv.f)
		fclose (ogv.f);
	if (ogv.pic)
		Z_Free (ogv.pic);
	memset (&ogv, 0, sizeof(ogv));
	ogv_open = false;
}

// the decoded frame, in the cube's colours
static void OGV_Convert (void)
{
	th_ycbcr_buffer	yuv;
	int		x, y, sx, sy, cx, cy, Y, Cb, Cr, R, G, B, d;
	byte	*out;
	int		xdec = !(ogv.ti.pixel_fmt & 1), ydec = !(ogv.ti.pixel_fmt & 2);

	if (th_decode_ycbcr_out (ogv.td, yuv))
		return;

	out = ogv.pic;
	for (y = 0 ; y < ogv.height ; y++)
	{
		sy = ogv.ti.pic_y + y * ogv.step;
		cy = sy >> ydec;
		for (x = 0 ; x < ogv.width ; x++)
		{
			sx = ogv.ti.pic_x + x * ogv.step;
			cx = sx >> xdec;
			Y = (yuv[0].data[sy * yuv[0].stride + sx] - 16) * 298;
			Cb = yuv[1].data[cy * yuv[1].stride + cx] - 128;
			Cr = yuv[2].data[cy * yuv[2].stride + cx] - 128;
			R = (Y + 409 * Cr + 128) >> 8;
			G = (Y - 100 * Cb - 208 * Cr + 128) >> 8;
			B = (Y + 516 * Cb + 128) >> 8;
			R = R < 0 ? 0 : R > 255 ? 255 : R;
			G = G < 0 ? 0 : G > 255 ? 255 : G;
			B = B < 0 ? 0 : B > 255 ? 255 : B;

			// to the cube's levels, the 4x4 ordered dither between them
			d = bayer[y & 3][x & 3] * 16 + 8;
			R = (R * 5 + d) / 256;
			G = (G * 6 + d) / 256;
			B = (B * 5 + d) / 256;
			*out++ = R * 42 + G * 6 + B;
		}
	}
}

// sound decoded and sent, until a quarter of a second is waiting to play
static void OGV_Sound (void)
{
	ogg_packet	op;
	float		**pcm;
	short		buf[2048 * 2];
	int			n, i, c, v, channels, ahead;

	if (!ogv.vorbis_ready || !dma.speed)
		return;
	channels = ogv.vi.channels > 1 ? 2 : 1;

	for ( ; ; )
	{
		ahead = s_rawend - paintedtime;
		if (ahead > dma.speed / 4)
			return;

		if ((n = vorbis_synthesis_pcmout (&ogv.vd, &pcm)) > 0)
		{
			if (n > 2048)
				n = 2048;
			for (i = 0 ; i < n ; i++)
				for (c = 0 ; c < channels ; c++)
				{
					v = (int)(pcm[c][i] * 32767.0f);
					buf[i*channels + c] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
				}
			S_RawSamples (n, ogv.vi.rate, 2, channels, (byte *)buf);
			vorbis_synthesis_read (&ogv.vd, n);
			continue;
		}
		if (ogg_stream_packetout (&ogv.vo, &op) > 0)
		{
			if (vorbis_synthesis (&ogv.vb, &op) == 0)
				vorbis_synthesis_blockin (&ogv.vd, &ogv.vb);
			continue;
		}
		if (!OGV_ReadData ())
			return;
		OGV_QueuePages ();
	}
}

/*
==================
OGV_Run

The frame for msec into the cinematic: its picture, NULL if it is the one
already returned, and false at the end
==================
*/
qboolean OGV_Run (int msec, byte **pic)
{
	ogg_packet		op;
	ogg_int64_t		granule;
	double			t = msec / 1000.0;
	qboolean		got = false;

	*pic = NULL;
	if (!ogv_open)
		return false;

	OGV_Sound ();

	// the frames due by now, the last of them shown
	for ( ; ; )
	{
		if (ogv.frame_ready)
		{
			if (ogv.frame_time > t)
				break;		// not yet
			ogv.frame_ready = false;
			got = true;
			OGV_Convert ();
		}
		if (ogv.video_done)
			break;
		if (ogg_stream_packetout (&ogv.to, &op) > 0)
		{
			granule = -1;
			if (th_decode_packetin (ogv.td, &op, &granule) >= 0 && granule >= 0)
			{
				ogv.frame_time = th_granule_time (ogv.td, granule);
				ogv.frame_ready = true;
			}
			continue;
		}
		if (!OGV_ReadData ())
			ogv.video_done = true;
		else
			OGV_QueuePages ();
	}

	if (got)
		*pic = ogv.pic;
	if (ogv.video_done && !ogv.frame_ready)
		return got;		// the last frame, then the end
	return true;
}

#else	// no libtheora: no .ogv cinematics

qboolean OGV_Open (char *name, int *width, int *height, byte *palette)
{
	return false;
}

qboolean OGV_Run (int msec, byte **pic)
{
	*pic = NULL;
	return false;
}

void OGV_Close (void)
{
}

#endif
