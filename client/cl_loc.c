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
cl_loc.c -- the 2023 re-release's message keys, in words.

The re-release's maps do not carry their messages: a target_help or a
trigger says "$map_establish_communication", and its engine looks that up
in localization/loc_english.txt, which ships in Q2Game.kpf beside its pak.
The container and the desktop launcher take that one file out of the .kpf
(docker/entrypoint.sh, desktop/quake2); here each $key in a centre print, a
console print or the help computer's layout is replaced by its text. Lines
in the file read

	map_establish_communication = "Establish communication\nlink to command ship." // 1 hits

loc_language picks the file. A message the file does not have, or every
one when there is no file, is worded from its name, which the re-release
makes from its first words: "$map_establish_communication" as "Establish
communication", "$g_secondary_mission_objective" as "Secondary mission
objective", its arguments after it.
*/

#include "client.h"

typedef struct
{
	char	*key;
	char	*value;
} locentry_t;

static locentry_t	*loc_entries;
static int			loc_numentries;
static qboolean		loc_tried;
static cvar_t		*loc_language;

static int LocCompare (const void *a, const void *b)
{
	return strcmp (((const locentry_t *)a)->key, ((const locentry_t *)b)->key);
}

/*
UTF-8 down to what the 1997 console font has: the typographic quotes and
dashes to their ASCII forms, anything else to '?'.
*/
static int LocFoldUTF8 (const unsigned char **pp)
{
	const unsigned char	*p = *pp;
	unsigned	c;
	int			n, i;

	if (p[0] < 0x80)
	{
		*pp = p + 1;
		return p[0];
	}
	if ((p[0] & 0xe0) == 0xc0) { c = p[0] & 0x1f; n = 1; }
	else if ((p[0] & 0xf0) == 0xe0) { c = p[0] & 0x0f; n = 2; }
	else if ((p[0] & 0xf8) == 0xf0) { c = p[0] & 0x07; n = 3; }
	else { *pp = p + 1; return '?'; }
	for (i = 1 ; i <= n ; i++)
	{
		if ((p[i] & 0xc0) != 0x80)
		{
			*pp = p + i;
			return '?';
		}
		c = (c << 6) | (p[i] & 0x3f);
	}
	*pp = p + n + 1;
	switch (c)
	{
	case 0x2018: case 0x2019: case 0x00b4: return '\'';
	case 0x201c: case 0x201d: return '"';
	case 0x2013: case 0x2014: return '-';
	case 0x2026: return '.';
	case 0x00a0: return ' ';
	}
	return '?';
}

static void Loc_Load (void)
{
	char		path[MAX_QPATH];
	char		*buf, *p, *key, *value, *out;
	int			len, keylen, max, count;
	const unsigned char	*u;

	loc_tried = true;
	if (!loc_language)
		loc_language = Cvar_Get ("loc_language", "english", CVAR_ARCHIVE);

	Com_sprintf (path, sizeof(path), "localization/loc_%s.txt", loc_language->string);
	len = FS_LoadFile (path, (void **)&buf);
	if (!buf)
	{
		Com_DPrintf ("No %s (it is in the re-release's Q2Game.kpf):\n"
			"its maps' messages are worded from their names\n", path);
		return;
	}

	// as many entries as lines, at most
	max = 1;
	for (p = buf ; p < buf + len ; p++)
		if (*p == '\n')
			max++;
	loc_entries = Z_Malloc (max * sizeof(locentry_t));
	count = 0;

	p = buf;
	while (p < buf + len && count < max)
	{
		char	*line = p, *eol;

		while (p < buf + len && *p != '\n')
			p++;
		eol = p;
		if (p < buf + len)
			p++;

		// key
		while (line < eol && (*line == ' ' || *line == '\t'))
			line++;
		key = line;
		while (line < eol && (isalnum ((unsigned char)*line) || *line == '_'))
			line++;
		if (line == key)
			continue;
		keylen = line - key;
		while (line < eol && (*line == ' ' || *line == '\t'))
			line++;
		if (line >= eol || *line != '=')
			continue;
		line++;
		while (line < eol && (*line == ' ' || *line == '\t'))
			line++;
		if (line >= eol || *line != '"')
			continue;
		line++;

		// value, unescaped and folded to the font's characters
		value = out = Z_Malloc ((eol - line) + 1);
		u = (const unsigned char *)line;
		while ((const char *)u < eol && *u != '"')
		{
			if (*u == '\\' && (const char *)u + 1 < eol)
			{
				u++;
				*out++ = *u == 'n' ? '\n' : *u;
				u++;
			}
			else
				*out++ = LocFoldUTF8 (&u);
		}
		*out = 0;

		loc_entries[count].key = Z_Malloc (keylen + 1);
		memcpy (loc_entries[count].key, key, keylen);
		loc_entries[count].key[keylen] = 0;
		loc_entries[count].value = value;
		count++;
	}
	FS_FreeFile (buf);

	loc_numentries = count;
	qsort (loc_entries, count, sizeof(locentry_t), LocCompare);
	Com_Printf ("%s: %i messages\n", path, count);
}

static const char *Loc_Find (const char *key, int len)
{
	char		name[128];
	locentry_t	probe, *found;

	if (len >= (int)sizeof(name) || !loc_numentries)
		return NULL;
	memcpy (name, key, len);
	name[len] = 0;
	probe.key = name;
	found = bsearch (&probe, loc_entries, loc_numentries, sizeof(locentry_t), LocCompare);
	return found ? found->value : NULL;
}

/*
The words for the key k, n long, at out: the file's, or for a map's
message its name made into words. NULL for neither.
*/
static char *Loc_Words (const char *k, int n, char *out, char *end)
{
	const char	*value;
	int			i, p;

	if ((value = Loc_Find (k, n)))
	{
		while (*value && out < end)
			*out++ = *value++;
		return out;
	}
	// map_establish_communication: "Establish communication", and the
	// game's own the same way, g_secondary_mission_objective: "Secondary
	// mission objective" -- the key less the word before its first _
	for (p = 0 ; p < n && islower ((unsigned char)k[p]) ; p++)
		;
	if (p > 0 && p < n - 1 && k[p] == '_')
	{
		for (i = p + 1 ; i < n && out < end ; i++)
		{
			if (k[i] == '_')
				*out++ = ' ';
			else if (i == p + 1)
				*out++ = toupper ((unsigned char)k[i]);
			else
				*out++ = k[i];
		}
		return out;
	}
	return NULL;
}

/*
An argument of a message: a key's words, or itself.
*/
static char *Loc_Argument (const char *a, int len, char *out, char *end)
{
	int		n;
	char	*o;

	if (a[0] == '$')
	{
		for (n = 0 ; n < len - 1 && (isalnum ((unsigned char)a[1+n]) || a[1+n] == '_') ; n++)
			;
		if (n == len - 1 && (o = Loc_Words (a + 1, n, out, end)))
			return o;
	}
	while (len-- > 0 && out < end)
		*out++ = *a++;
	return out;
}

/*
The string with each $key in it that the file has replaced by its text;
the string itself when there is none. The result lasts until the next call
but one.

A key may be followed by its arguments, each after a 0x1f, to the end of
the line: the re-release's prints with something to fill in, as its demos
have them (server/sv_rrdemo.c). Its text then has a "{}" for each.
*/
char *CL_Localize (char *s)
{
	static char	bufs[2][2048];
	static int	which;
	char		*out, *end, *o, *value;
	const char	*p, *k, *args[8], *a;
	int			n, i, nargs, alen[8];
	char		words[1024];

	if (!s || !strchr (s, '$'))
		return s;
	if (!loc_tried)
		Loc_Load ();

	which ^= 1;
	out = bufs[which];
	end = out + sizeof(bufs[0]) - 1;
	for (p = s ; *p && out < end ; )
	{
		if (*p == '$')
		{
			for (k = p + 1, n = 0 ; isalnum ((unsigned char)k[n]) || k[n] == '_' ; n++)
				;
			o = n ? Loc_Words (k, n, words, words + sizeof(words) - 1) : NULL;
			if (o)
			{
				*o = 0;
				p = k + n;

				// its arguments
				nargs = 0;
				while (*p == 0x1f && nargs < 8)
				{
					a = ++p;
					while (*p && *p != 0x1f && *p != '\n')
						p++;
					args[nargs] = a;
					alen[nargs++] = p - a;
				}

				for (value = words, i = 0 ; *value && out < end ; )
				{
					if (value[0] == '{' && value[1] == '}')
					{
						if (i < nargs)
							out = Loc_Argument (args[i], alen[i], out, end);
						i++;
						value += 2;
					}
					else
						*out++ = *value++;
				}
				// arguments its words had no {} for, the words made from
				// a key the file has not got: after them
				for ( ; i < nargs && out < end - 2 ; i++)
				{
					*out++ = i ? ',' : ':';
					*out++ = ' ';
					out = Loc_Argument (args[i], alen[i], out, end);
				}
				continue;
			}
		}
		if (*p == 0x1f)
			*out++ = ' ';	// an argument for a key not known
		else
			*out++ = *p;
		p++;
	}
	*out = 0;
	return bufs[which];
}
