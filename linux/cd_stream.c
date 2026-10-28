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
// cd_stream.c -- music, from files rather than from a CD.
//
// Quake II's music is not in the game data. It is audio tracks 2 to 11 of the
// CD-ROM, and cd_linux.c plays them by opening /dev/cdrom and issuing
// CDROMPLAYTRKIND -- a drive command, to a drive, which tells its own DAC to
// play. No container has one, and neither do most machines any more.
//
// So the same track numbers are looked up as files instead, and decoded and
// mixed into the engine's own output. On the CD the music never went through
// the mixer at all, which is why cd_volume in the 1997 code could only switch
// the drive between paused and playing. Here it scales samples.
//
// Tracks are looked for as <dir>/track02.<ext>, numbered the way the CD was,
// and matched without regard to case: the GOG release ships Track02.ogg, a
// rip from most tools is track02.ogg, and every other source port settled on
// the same names, so an existing music directory works without renaming.
// A bare 02.<ext> is taken too, as Yamagi's and Quake II RTX's CD rips are
// named. The re-release numbers all its music as one soundtrack -- the base
// game's 2 to 11, the mission packs' 12 to 21, Quake 64's 64 to 79 -- and a
// map of its can name a file instead (worldspawn's "music"), which is
// looked for as it is named, or with each extension.
//
// Decoding is libsndfile's: Ogg Vorbis, FLAC, WAV, Opus and, where the
// installed build has it, MP3. Build with MUSIC=none to leave it out, and
// CDAudio_Init reports that there is no music rather than that there are no
// files.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#include "../client/client.h"
#include "../client/snd_loc.h"

#ifdef QUAKE_MUSIC_SNDFILE
#include <sndfile.h>
#endif

// Track 1 is the data track on the Quake II CD; the music starts at 2.
#define FIRST_MUSIC_TRACK	2
#define MAX_MUSIC_TRACK		99

static cvar_t	*cd_nocd;
static cvar_t	*cd_volume;
static cvar_t	*cd_musicdir;
// As the Windows CD player had them: a looping track is played
// cd_loopcount times, then cd_looptrack instead -- unless the map says
// otherwise (the re-release's CS_CD_LOOP_COUNT, music_loopcount, -1 if it
// does not; 0 for good)
static cvar_t	*cd_loopcount;
static cvar_t	*cd_looptrack;
static int		music_loopcount = -1;
static int		music_loops;			// times round, of this track
static qboolean	music_to_looptrack;		// it has been round enough: CDAudio_Update

static qboolean	music_enabled = false;		// built with a decoder, and not -nocdaudio
static qboolean	music_paused = false;
static qboolean	music_looping = false;
// What is playing, and what the map asked for, playing or not: a track's
// number ("4"), or the re-release's file name ("D_DDTBLU.ogg"). Empty is
// nothing.
static char		music_track[MAX_QPATH];
static char		music_wanted[MAX_QPATH];
static char		music_dir[MAX_OSPATH];
static char		music_dir_game[MAX_OSPATH];	// the game directory music_dir was found for

// Extensions in the order they are tried. WAV last: a directory holding both
// a compressed rip and the wav it was made from wants the compressed one.
static const char *music_exts[] = { "ogg", "opus", "flac", "mp3", "wav", NULL };

#ifdef QUAKE_MUSIC_SNDFILE

static SNDFILE	*music_sf;
static SF_INFO	music_info;

// Where we are between two decoded source frames, so that a source rate that
// is not the output rate still comes out at the right pitch and length.
static float	music_frac;
static float	music_prev[2];		// the frame behind music_frac
static float	music_next[2];		// the frame ahead of it
static float	music_step;			// source frames per output frame
static qboolean	music_eof;

#endif


/*
==================
CDAudio_FindFile

The name in dir that matches want, ignoring case, written into out. Returns
false if there is none.
==================
*/
static qboolean CDAudio_FindFile (const char *dir, const char *want, char *out, int outsize)
{
	DIR				*d;
	struct dirent	*e;
	qboolean		found = false;

	d = opendir (dir);
	if (!d)
		return false;

	while ((e = readdir (d)) != NULL)
	{
		if (!Q_stricmp (e->d_name, (char *)want))
		{
			Com_sprintf (out, outsize, "%s/%s", dir, e->d_name);
			found = true;
			break;
		}
	}

	closedir (d);
	return found;
}


/*
==================
CDAudio_DirHasFiles

Whether dir holds anything that looks like a track. Checked rather than
assumed: the container links a music directory into every game directory it
finds, so an empty one is easy to end up with, and an empty directory that
won would mean silence with no fallback rather than the base game's
soundtrack.
==================
*/
static qboolean CDAudio_DirHasFiles (const char *dir)
{
	struct stat		st;
	DIR				*d;
	struct dirent	*e;
	qboolean		found = false;

	if (!dir || !*dir)
		return false;

	if (stat (dir, &st) != 0 || !S_ISDIR (st.st_mode))
		return false;

	d = opendir (dir);
	if (!d)
		return false;

	while (!found && (e = readdir (d)) != NULL)
	{
		const char	*ext;
		int			i;

		if (Q_strncasecmp (e->d_name, "track", 5)
			&& !(e->d_name[0] >= '0' && e->d_name[0] <= '9'))
			continue;

		ext = strrchr (e->d_name, '.');
		if (!ext)
			continue;

		for (i = 0; music_exts[i]; i++)
			if (!Q_stricmp ((char *)ext + 1, (char *)music_exts[i]))
			{
				found = true;
				break;
			}
	}

	closedir (d);
	return found;
}


/*
==================
CDAudio_FindMusicDir

The first of these that holds a track:

  +set cd_musicdir <path>   an explicit override
  <gamedir>/music           beside the pak files, which is where a rip goes
  $QUAKE2_MUSICDIR          set by the container's entrypoint, or by hand
  <basedir>/baseq2/music    so that a mod still finds the base game's music
  <basedir>/music           where the GOG release keeps it

The game directory beats the environment on purpose. The mission packs have
soundtracks of their own, and a rip of each goes in its own directory --
but $QUAKE2_MUSICDIR is necessarily one directory for every game the
container can run, so if it won, a mission pack would play Quake II's music
instead of its own.

Looked for again whenever the game directory changes, since the game menu
and a demo or server switching mod can both change it under a running
engine.
==================
*/
static void CDAudio_FindMusicDir (void)
{
	const char	*env;
	char		trial[MAX_OSPATH];
	cvar_t		*basedir;

	music_dir[0] = 0;
	Com_sprintf (music_dir_game, sizeof(music_dir_game), "%s", FS_Gamedir ());

	if (CDAudio_DirHasFiles (cd_musicdir->string))
	{
		Com_sprintf (music_dir, sizeof(music_dir), "%s", cd_musicdir->string);
		return;
	}

	Com_sprintf (trial, sizeof(trial), "%s/music", FS_Gamedir ());
	if (CDAudio_DirHasFiles (trial))
	{
		Com_sprintf (music_dir, sizeof(music_dir), "%s", trial);
		return;
	}

	env = getenv ("QUAKE2_MUSICDIR");
	if (CDAudio_DirHasFiles (env))
	{
		Com_sprintf (music_dir, sizeof(music_dir), "%s", (char *)env);
		return;
	}

	basedir = Cvar_Get ("basedir", ".", CVAR_NOSET);

	Com_sprintf (trial, sizeof(trial), "%s/%s/music", basedir->string, BASEDIRNAME);
	if (CDAudio_DirHasFiles (trial))
	{
		Com_sprintf (music_dir, sizeof(music_dir), "%s", trial);
		return;
	}

	Com_sprintf (trial, sizeof(trial), "%s/music", basedir->string);
	if (CDAudio_DirHasFiles (trial))
		Com_sprintf (music_dir, sizeof(music_dir), "%s", trial);
}


/*
==================
CDAudio_CheckGamedir

Called before anything is opened, so a change of game directory since the
last track is noticed and the right soundtrack found.
==================
*/
static void CDAudio_CheckGamedir (void)
{
	if (strcmp (music_dir_game, FS_Gamedir ()))
	{
		CDAudio_FindMusicDir ();
		if (music_dir[0])
			Com_Printf ("CDAudio: music from %s\n", music_dir);
	}
}


/*
==================
CDAudio_TrackNumber

A track's number, if the name is one; 0 if it is a file's name.
==================
*/
static int CDAudio_TrackNumber (const char *track)
{
	const char	*c;

	if (!*track)
		return 0;
	for (c = track; *c; c++)
		if (*c < '0' || *c > '9')
			return 0;
	return atoi (track);
}


#ifdef QUAKE_MUSIC_SNDFILE

/*
==================
CDAudio_CloseTrack
==================
*/
static void CDAudio_CloseTrack (void)
{
	if (music_sf)
	{
		sf_close (music_sf);
		music_sf = NULL;
	}
	music_track[0] = 0;
	music_eof = false;
}


/*
==================
CDAudio_TryFile

Opens want in the music directory, if it is there and decodes.
==================
*/
static qboolean CDAudio_TryFile (const char *want, char *path, int pathsize)
{
	if (!CDAudio_FindFile (music_dir, want, path, pathsize))
		return false;

	memset (&music_info, 0, sizeof(music_info));
	music_sf = sf_open (path, SFM_READ, &music_info);
	if (music_sf)
		return true;

	Com_Printf ("CDAudio: cannot decode %s: %s\n", path, sf_strerror (NULL));
	return false;
}

/*
==================
CDAudio_OpenTrack

A track's number as trackNN or NN with each extension in turn; a name as it
is, or with each. Returns false with nothing open if none of them is there,
which is not an error: a partial rip is the normal case.
==================
*/
static qboolean CDAudio_OpenTrack (const char *track)
{
	char	want[MAX_QPATH + 8];
	char	path[MAX_OSPATH];
	int		i, number;

	CDAudio_CloseTrack ();

	if (!music_dir[0] || !dma.speed)
		return false;

	number = CDAudio_TrackNumber (track);
	if (!number && strchr (track, '.'))
		CDAudio_TryFile (track, path, sizeof(path));

	for (i = 0; music_exts[i] && !music_sf; i++)
	{
		if (number)
		{
			Com_sprintf (want, sizeof(want), "track%02d.%s", number, music_exts[i]);
			if (CDAudio_TryFile (want, path, sizeof(path)))
				break;
			Com_sprintf (want, sizeof(want), "%02d.%s", number, music_exts[i]);
		}
		else
			Com_sprintf (want, sizeof(want), "%s.%s", track, music_exts[i]);
		CDAudio_TryFile (want, path, sizeof(path));
	}

	if (!music_sf)
		return false;

	if (music_info.channels < 1 || music_info.channels > 2)
	{
		Com_Printf ("CDAudio: track %s has %d channels; only mono and stereo\n",
					track, music_info.channels);
		CDAudio_CloseTrack ();
		return false;
	}

	if (music_info.samplerate < 1)
	{
		Com_Printf ("CDAudio: track %s reports a sample rate of %d\n",
					track, music_info.samplerate);
		CDAudio_CloseTrack ();
		return false;
	}

	music_step = (float)music_info.samplerate / (float)dma.speed;
	music_frac = 1.0f;				// forces both frames to be read first
	music_prev[0] = music_prev[1] = 0;
	music_next[0] = music_next[1] = 0;
	music_eof = false;
	Com_sprintf (music_track, sizeof(music_track), "%s", (char *)track);

	Com_Printf ("CDAudio: playing %s (%d Hz, %s)\n", path,
				music_info.samplerate,
				music_info.channels == 2 ? "stereo" : "mono");
	return true;
}


/*
==================
CDAudio_NextSourceFrame

One frame from the file, as a stereo pair. Returns false at the end of the
file, having restarted it first if the track is looping.
==================
*/
static qboolean CDAudio_NextSourceFrame (float *out)
{
	float		raw[2];
	sf_count_t	got;

	got = sf_readf_float (music_sf, raw, 1);
	if (got < 1)
	{
		int		loops = music_loopcount >= 0 ? music_loopcount : (int)cd_loopcount->value;

		if (!music_looping)
			return false;

		// round enough: the loop track, which CDAudio_Update starts, as
		// opening a file here, in the middle of mixing, would be heard
		if (loops > 0 && ++music_loops >= loops
			&& CDAudio_TrackNumber (music_track) != (int)cd_looptrack->value)
		{
			music_to_looptrack = true;
			return false;
		}

	// sf_seek back to the start rather than reopening: reopening an Ogg
	// costs a decoder reset and a file open in the middle of a frame, and
	// the loop is audible if it takes longer than the mixer's headroom.
		if (sf_seek (music_sf, 0, SEEK_SET) < 0)
			return false;

		got = sf_readf_float (music_sf, raw, 1);
		if (got < 1)
			return false;
	}

	if (music_info.channels == 1)
	{
		out[0] = raw[0];
		out[1] = raw[0];
	}
	else
	{
		out[0] = raw[0];
		out[1] = raw[1];
	}

	return true;
}


/*
==================
CDAudio_MixPaintBuffer

Called from S_PaintChannels with the stretch of paint buffer about to have
the sound effects mixed into it. The music goes in at the paint buffer's own
scale, a 16-bit sample shifted up by 8, which is the scale S_RawSamples
writes the cinematics' sound at: the transfer stage shifts it back down and
clips, so a full-scale sample here is a full-scale sample out.

Resampling is linear. A CD rip is 44100 and the output is 22050, so this is
almost always a straight 2:1 decimation; linear interpolation is inaudible on
music at that ratio and costs two multiplies a sample.
==================
*/
void CDAudio_MixPaintBuffer (portable_samplepair_t *buffer, int count)
{
	int		i;
	float	vol;

	if (!music_enabled || !music_sf || music_paused || music_eof)
		return;

	vol = cd_volume->value;
	if (vol <= 0)
		return;
	if (vol > 1)
		vol = 1;

// 32767 rather than 32768: the transfer stage clips at 32767, and a track
// mastered to full scale would otherwise clip on every trough.
	vol *= 32767.0f * 256.0f;

	for (i = 0; i < count; i++)
	{
		float	l, r;

		while (music_frac >= 1.0f)
		{
			music_prev[0] = music_next[0];
			music_prev[1] = music_next[1];

			if (!CDAudio_NextSourceFrame (music_next))
			{
				music_eof = true;
				CDAudio_CloseTrack ();
				return;
			}

			music_frac -= 1.0f;
		}

		l = music_prev[0] + (music_next[0] - music_prev[0]) * music_frac;
		r = music_prev[1] + (music_next[1] - music_prev[1]) * music_frac;

		buffer[i].left  += (int)(l * vol);
		buffer[i].right += (int)(r * vol);

		music_frac += music_step;
	}
}

#else	// !QUAKE_MUSIC_SNDFILE

void CDAudio_MixPaintBuffer (portable_samplepair_t *buffer, int count)
{
}

static void CDAudio_CloseTrack (void)
{
	music_track[0] = 0;
}

static qboolean CDAudio_OpenTrack (const char *track)
{
	return false;
}

#endif	// QUAKE_MUSIC_SNDFILE


/*
==================
CDAudio_PlayName

A track by its number ("4") or the re-release's by its file's name.
==================
*/
void CDAudio_PlayName (char *name, qboolean looping)
{
	int		number;
	char	first[MAX_QPATH], *space;

	if (!music_enabled)
		return;

	// "file number": the re-release's music by its name, and failing that
	// the map's track number (ndctf0's D_DDTBLU.ogg is in no release)
	if ((space = strchr (name, ' ')) != NULL)
	{
		Com_sprintf (first, sizeof(first), "%.*s", (int)(space - name), name);
		CDAudio_PlayName (first, looping);
		if (!music_track[0] && !cd_nocd->value)
			CDAudio_PlayName (space + 1, looping);
		return;
	}

	// A map with no "sounds" key on its worldspawn asks for track 0, which
	// is silence; so does the cinematic code, on its way out.
	number = CDAudio_TrackNumber (name);
	if (!name[0] || (number == 0 && name[0] == '0')
		|| (number && (number < FIRST_MUSIC_TRACK || number > MAX_MUSIC_TRACK)))
	{
		if (number)
			Com_DPrintf ("CDAudio: track %d out of range\n", number);
		music_wanted[0] = 0;
		CDAudio_CloseTrack ();
		return;
	}

	Com_sprintf (music_wanted, sizeof(music_wanted), "%s", name);
	music_looping = looping;

	if (cd_nocd->value)
		return;

	CDAudio_CheckGamedir ();

	if (!strcmp (music_track, music_wanted) && !music_paused)
		return;						// already playing it

	music_paused = false;
	music_loops = 0;
	music_to_looptrack = false;

	if (!CDAudio_OpenTrack (music_wanted))
		Com_DPrintf ("CDAudio: no file for track %s in %s\n", music_wanted,
					 music_dir[0] ? music_dir : "(no music directory)");
}

void CDAudio_Play (int track, qboolean looping)
{
	CDAudio_PlayName (va ("%d", track), looping);
}


void CDAudio_Stop (void)
{
	if (!music_enabled)
		return;

	CDAudio_CloseTrack ();
	music_paused = false;
}


static void CDAudio_Pause (void)
{
	if (!music_enabled || !music_track[0])
		return;

	music_paused = true;
}


static void CDAudio_Resume (void)
{
	if (!music_enabled || !music_track[0])
		return;

	music_paused = false;
}


/*
==================
CDAudio_Update

The 1997 version polled the drive here, because the drive was the only thing
that knew whether the track had finished. The mixer notices the end of a file
itself, so what is left is the menu's "CD music" switch, which sets cd_nocd:
turned off, the music stops; turned back on, the map's track starts again.
==================
*/
void CDAudio_SetLoopCount (char *count)
{
	music_loopcount = (count && count[0]) ? atoi (count) : -1;
}

void CDAudio_Update (void)
{
	if (!music_enabled)
		return;

	// played round enough: the loop track, for good
	if (music_to_looptrack)
	{
		char	was[MAX_QPATH];

		music_to_looptrack = false;
		strncpy (was, music_wanted, sizeof(was) - 1);
		was[sizeof(was) - 1] = 0;
		CDAudio_PlayName (va ("%d", (int)cd_looptrack->value), true);
		// no loop track to be had: round again, as it was, for good
		if (!music_track[0] && !cd_nocd->value)
		{
			CDAudio_PlayName (was, true);
			music_loops = -0x7fffffff;
		}
	}

	if (cd_nocd->modified)
	{
		cd_nocd->modified = false;
		if (cd_nocd->value)
			CDAudio_Stop ();
		else if (music_wanted[0])
			CDAudio_PlayName (music_wanted, music_looping);
	}
}


/*
==================
CD_f

The same console command the CD player had, so that anything binding or
scripting `cd play 4` still works, minus the commands that only meant
something to a drive: eject, close, reset.
==================
*/
static void CD_f (void)
{
	char	*command;

	if (Cmd_Argc() < 2)
	{
		Com_Printf ("cd play <track> | loop <track> | stop | pause | resume | info\n");
		return;
	}

	command = Cmd_Argv (1);

	if (Q_stricmp (command, "on") == 0)
	{
		Cvar_Set ("cd_nocd", "0");
		return;
	}

	if (Q_stricmp (command, "off") == 0)
	{
		Cvar_Set ("cd_nocd", "1");
		return;
	}

	if (Q_stricmp (command, "play") == 0)
	{
		CDAudio_PlayName (Cmd_Argv (2), false);
		return;
	}

	if (Q_stricmp (command, "loop") == 0)
	{
		CDAudio_PlayName (Cmd_Argv (2), true);
		return;
	}

	if (Q_stricmp (command, "stop") == 0)
	{
		CDAudio_Stop ();
		return;
	}

	if (Q_stricmp (command, "pause") == 0)
	{
		CDAudio_Pause ();
		return;
	}

	if (Q_stricmp (command, "resume") == 0)
	{
		CDAudio_Resume ();
		return;
	}

	if (Q_stricmp (command, "eject") == 0
		|| Q_stricmp (command, "close") == 0
		|| Q_stricmp (command, "reset") == 0
		|| Q_stricmp (command, "remap") == 0)
	{
		Com_Printf ("cd %s: there is no drive; music comes from %s\n",
					command, music_dir[0] ? music_dir : "nowhere -- see cd info");
		return;
	}

	if (Q_stricmp (command, "info") == 0)
	{
		CDAudio_CheckGamedir ();
#ifndef QUAKE_MUSIC_SNDFILE
		Com_Printf ("No music: this build has no decoder.\n");
#else
		if (!music_dir[0])
			Com_Printf ("No music directory. Looked for cd_musicdir,\n"
						"<gamedir>/music, $QUAKE2_MUSICDIR,\n"
						"<basedir>/" BASEDIRNAME "/music and <basedir>/music.\n");
		else
			Com_Printf ("Music directory: %s\n", music_dir);
		if (music_track[0])
			Com_Printf ("Playing track %s%s%s\n", music_track,
						music_looping ? ", looping" : "",
						music_paused ? ", paused" : "");
		else if (music_wanted[0])
			Com_Printf ("Track %s wanted, %s\n", music_wanted,
						cd_nocd->value ? "but CD music is off" : "but there is no file for it");
		else
			Com_Printf ("Nothing playing\n");
		Com_Printf ("Volume is %g\n", cd_volume->value);
#endif
		return;
	}

	Com_Printf ("cd: unknown command \"%s\"\n", command);
}


/*
==================
CDAudio_Init
==================
*/
int CDAudio_Init (void)
{
	cvar_t	*cv;

	cv = Cvar_Get ("nocdaudio", "0", CVAR_NOSET);
	if (cv->value)
		return -1;

	cd_nocd = Cvar_Get ("cd_nocd", "0", CVAR_ARCHIVE);
	cd_nocd->modified = false;
	cd_volume = Cvar_Get ("cd_volume", "1", CVAR_ARCHIVE);
	cd_loopcount = Cvar_Get ("cd_loopcount", "4", 0);
	cd_looptrack = Cvar_Get ("cd_looptrack", "11", 0);
	cd_musicdir = Cvar_Get ("cd_musicdir", "", CVAR_NOSET);

	Cmd_AddCommand ("cd", CD_f);

#ifndef QUAKE_MUSIC_SNDFILE
	Com_Printf ("CDAudio: built without a decoder; no music.\n");
	return -1;
#else
	music_enabled = true;

	CDAudio_FindMusicDir ();

	if (!music_dir[0])
	{
	// Not an error. The demo has no music to go with it -- the tracks were
	// on the CD -- so this is the ordinary case for anyone who has not
	// ripped their own.
		Com_Printf ("CDAudio: no music directory; put track02.ogg and\n"
					"         friends in <gamedir>/music to hear the soundtrack.\n");
		return 0;
	}

	Com_Printf ("CDAudio: music from %s\n", music_dir);
	return 0;
#endif
}


void CDAudio_Activate (qboolean active)
{
	if (active)
		CDAudio_Resume ();
	else
		CDAudio_Pause ();
}


void CDAudio_Shutdown (void)
{
	if (!music_enabled)
		return;

	CDAudio_CloseTrack ();
	music_enabled = false;
}
