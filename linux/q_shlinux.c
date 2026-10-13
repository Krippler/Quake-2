#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>

#include "../linux/glob.h"

#include "../qcommon/qcommon.h"

//===============================================================================

byte *membase;
int maxhunksize;
int curhunksize;

// The size of the block sits in front of what Hunk_Begin hands out, so that
// Hunk_Free can munmap it. It was sizeof(int), which left everything in the
// hunk 4-byte aligned: harmless on a 486, but the hunk holds pointers and
// floats that x86-64 code is entitled to load with aligned SSE instructions.
// A cache line keeps the alignment Hunk_Alloc's rounding was meant to give.
#define HUNK_HEADER 32

void *Hunk_Begin (int maxsize)
{
	// reserve a huge chunk of memory, but don't commit any yet
	maxhunksize = maxsize + HUNK_HEADER;
	curhunksize = 0;
	membase = mmap(0, maxhunksize, PROT_READ|PROT_WRITE, 
		MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (membase == NULL || membase == (byte *)-1)
		Sys_Error("unable to virtual allocate %d bytes", maxsize);

	*((int *)membase) = maxhunksize;

	return membase + HUNK_HEADER;
}

void *Hunk_Alloc (int size)
{
	byte *buf;

	// round to cacheline
	size = (size+31)&~31;
	if (curhunksize + size > maxhunksize - HUNK_HEADER)
		Sys_Error("Hunk_Alloc overflow");
	buf = membase + HUNK_HEADER + curhunksize;
	curhunksize += size;
	return buf;
}

int Hunk_End (void)
{
	byte *n;

	n = mremap(membase, maxhunksize, curhunksize + HUNK_HEADER, 0);
	if (n != membase)
		Sys_Error("Hunk_End:  Could not remap virtual block (%d)", errno);
	*((int *)membase) = curhunksize + HUNK_HEADER;
	
	return curhunksize;
}

void Hunk_Free (void *base)
{
	byte *m;

	if (base) {
		m = ((byte *)base) - HUNK_HEADER;
		if (munmap(m, *((int *)m)))
			Sys_Error("Hunk_Free: munmap failed (%d)", errno);
	}
}

//===============================================================================


/*
================
Sys_Milliseconds

CLOCK_MONOTONIC, not gettimeofday: the wall clock steps when NTP corrects it,
and a step backwards gave the frame loop a negative frame time.
================
*/
int curtime;
int Sys_Milliseconds (void)
{
	struct timespec tp;
	static time_t	secbase;

	clock_gettime(CLOCK_MONOTONIC, &tp);
	
	if (!secbase)
	{
		secbase = tp.tv_sec;
		return tp.tv_nsec/1000000;
	}

	curtime = (tp.tv_sec - secbase)*1000 + tp.tv_nsec/1000000;
	
	return curtime;
}

void Sys_Mkdir (char *path)
{
    mkdir (path, 0777);
}

char *strlwr (char *s)
{
	char *start = s;

	while (*s) {
		*s = tolower(*s);
		s++;
	}
	return start;
}

//============================================

static	char	findbase[MAX_OSPATH];
static	char	findpath[MAX_OSPATH];
static	char	findpattern[MAX_OSPATH];
static	DIR		*fdir;

static qboolean CompareAttributes(char *path, char *name,
	unsigned musthave, unsigned canthave )
{
	struct stat st;
	char fn[MAX_OSPATH];

// . and .. never match
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return false;

	sprintf(fn, "%s/%s", path, name);
	if (stat(fn, &st) == -1)
		return false; // shouldn't happen

	if ( ( st.st_mode & S_IFDIR ) && ( canthave & SFF_SUBDIR ) )
		return false;

	if ( ( musthave & SFF_SUBDIR ) && !( st.st_mode & S_IFDIR ) )
		return false;

	return true;
}

char *Sys_FindFirst (char *path, unsigned musthave, unsigned canhave)
{
	struct dirent *d;
	char *p;

	if (fdir)
		Sys_Error ("Sys_BeginFind without close");

//	COM_FilePath (path, findbase);
	strcpy(findbase, path);

	if ((p = strrchr(findbase, '/')) != NULL) {
		*p = 0;
		strcpy(findpattern, p + 1);
	} else
		strcpy(findpattern, "*");

	if (strcmp(findpattern, "*.*") == 0)
		strcpy(findpattern, "*");
	
	if ((fdir = opendir(findbase)) == NULL)
		return NULL;
	while ((d = readdir(fdir)) != NULL) {
		if (!*findpattern || glob_match(findpattern, d->d_name)) {
//			if (*findpattern)
//				printf("%s matched %s\n", findpattern, d->d_name);
			if (CompareAttributes(findbase, d->d_name, musthave, canhave)) {
				sprintf (findpath, "%s/%s", findbase, d->d_name);
				return findpath;
			}
		}
	}
	return NULL;
}

char *Sys_FindNext (unsigned musthave, unsigned canhave)
{
	struct dirent *d;

	if (fdir == NULL)
		return NULL;
	while ((d = readdir(fdir)) != NULL) {
		if (!*findpattern || glob_match(findpattern, d->d_name)) {
//			if (*findpattern)
//				printf("%s matched %s\n", findpattern, d->d_name);
			if (CompareAttributes(findbase, d->d_name, musthave, canhave)) {
				sprintf (findpath, "%s/%s", findbase, d->d_name);
				return findpath;
			}
		}
	}
	return NULL;
}

void Sys_FindClose (void)
{
	if (fdir != NULL)
		closedir(fdir);
	fdir = NULL;
}


//============================================

