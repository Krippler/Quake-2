#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <mntent.h>

#include <dlfcn.h>
#include <execinfo.h>
#include <stdint.h>
#include <time.h>

#include "../qcommon/qcommon.h"

#include "../linux/rw_linux.h"

cvar_t *nostdout;

unsigned	sys_frame_time;

uid_t saved_euid;
qboolean stdin_active = true;

// Raised by Sys_SignalQuit, acted on by the frame loop in main.
static volatile sig_atomic_t sys_signalquit;

// =======================================================================
// General routines
// =======================================================================

void Sys_ConsoleOutput (char *string)
{
	if (nostdout && nostdout->value)
		return;

	fputs(string, stdout);
}

void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	unsigned char		*p;

	va_start (argptr,fmt);
	vsprintf (text,fmt,argptr);
	va_end (argptr);

	if (strlen(text) > sizeof(text))
		Sys_Error("memory overwrite in Sys_Printf");

    if (nostdout && nostdout->value)
        return;

	for (p = (unsigned char *)text; *p; p++) {
		*p &= 0x7f;
		if ((*p > 128 || *p < 32) && *p != 10 && *p != 13 && *p != 9)
			printf("[%02x]", *p);
		else
			putc(*p, stdout);
	}
}

void Sys_Quit (void)
{
	// Com_Quit, the only caller, has shut the client down already; doing it
	// again only printed "recursive shutdown".
	Qcommon_Shutdown ();
    fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
	_exit(0);
}

void Sys_Init(void)
{
#if id386
//	Sys_SetFPCW();
#endif
}

void Sys_Error (char *error, ...)
{ 
    va_list     argptr;
    char        string[1024];

// change stdin to non blocking
    fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);

	CL_Shutdown ();
	Qcommon_Shutdown ();
    
    va_start (argptr,error);
    vsprintf (string,error,argptr);
    va_end (argptr);
	fprintf(stderr, "Error: %s\n", string);

	_exit (1);

} 

void Sys_Warn (char *warning, ...)
{ 
    va_list     argptr;
    char        string[1024];
    
    va_start (argptr,warning);
    vsprintf (string,warning,argptr);
    va_end (argptr);
	fprintf(stderr, "Warning: %s", string);
} 

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int	Sys_FileTime (char *path)
{
	struct	stat	buf;
	
	if (stat (path,&buf) == -1)
		return -1;
	
	return buf.st_mtime;
}

void floating_point_exception_handler(int whatever)
{
//	Sys_Warn("floating point exception\n");
	signal(SIGFPE, floating_point_exception_handler);
}

char *Sys_ConsoleInput(void)
{
    static char text[256];
    int     len;
	fd_set	fdset;
    struct timeval timeout;

	if (!dedicated || !dedicated->value)
		return NULL;

	if (!stdin_active)
		return NULL;

	FD_ZERO(&fdset);
	FD_SET(0, &fdset); // stdin
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	if (select (1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(0, &fdset))
		return NULL;

	len = read (0, text, sizeof(text));
	if (len == 0) { // eof!
		stdin_active = false;
		return NULL;
	}

	if (len < 1)
		return NULL;
	text[len-1] = 0;    // rip off the /n and terminate

	return text;
}

/*****************************************************************************/

static void *game_library;

/*
=================
Sys_UnloadGame
=================
*/
void Sys_UnloadGame (void)
{
	if (game_library) 
		dlclose (game_library);
	game_library = NULL;
}

/*
=================
Sys_GetGameAPI

Loads the game dll
=================
*/
void *Sys_GetGameAPI (void *parms)
{
	void	*(*GetGameAPI) (void *);

	char	name[MAX_OSPATH];
	char	*path;
	char	*gamedir;
	const char *gamename = "gamex86_64.so";

	if (game_library)
		Com_Error (ERR_FATAL, "Sys_GetGameAPI without Sys_UnloadingGame");

	Com_Printf("------- Loading %s -------\n", gamename);

	// The search paths first, so a mod that brings its own library -- built
	// for this engine, or for Yamagi Quake II, whose game API is the same --
	// is the one that runs. The original also put the current directory in
	// front of each path, which made a relative basedir work and an absolute
	// one fail; FS_NextPath's paths are used as they are.
	path = NULL;
	while (1)
	{
		path = FS_NextPath (path);
		if (!path)
			break;
		Com_sprintf (name, sizeof(name), "%s/%s", path, gamename);
		if (access (name, R_OK) != 0)
			continue;
		game_library = dlopen (name, RTLD_NOW);
		if (game_library)
		{
			Com_Printf ("LoadLibrary (%s)\n", name);
			break;
		}
		Com_Printf ("LoadLibrary (%s) failed: %s\n", name, dlerror ());
	}

	// Then the libraries this build installed beside the engine. The game
	// data comes from id's pak files and is only ever read; the code that
	// runs it is this port's, and lives with the binary rather than being
	// copied into somebody's game directory.
	if (!game_library)
	{
		gamedir = Cvar_VariableString ("gamedir");
		if (!gamedir[0])
			gamedir = BASEDIRNAME;

		Com_sprintf (name, sizeof(name), "%s/%s/%s", Sys_ExeDir (), gamedir, gamename);
		if (access (name, R_OK) != 0 && strcmp (gamedir, BASEDIRNAME))
		{
			// A mod that is only data -- maps, models, sounds -- runs on the
			// single-player game code, which is what the original did too
			// once it ran out of search paths that held a library.
			Com_sprintf (name, sizeof(name), "%s/%s/%s", Sys_ExeDir (), BASEDIRNAME, gamename);
		}

		game_library = dlopen (name, RTLD_NOW);
		if (!game_library)
		{
			Com_Printf ("LoadLibrary (%s) failed: %s\n", name, dlerror ());
			return NULL;
		}
		Com_Printf ("LoadLibrary (%s)\n", name);
	}

	GetGameAPI = (void *)dlsym (game_library, "GetGameAPI");
	if (!GetGameAPI)
	{
		Sys_UnloadGame ();		
		return NULL;
	}

	return GetGameAPI (parms);
}

/*
=================
Sys_ExeDir

Where the engine binary is, and so where the renderer and the game libraries
are. The original read it from /etc/quake2.conf, which only root could write
and which a container, a tarball unpacked in a home directory or a second
install all lack.
=================
*/
char *Sys_ExeDir (void)
{
	static char	dir[MAX_OSPATH];
	ssize_t		n;
	char		*slash;

	if (dir[0])
		return dir;

	n = readlink ("/proc/self/exe", dir, sizeof(dir) - 1);
	if (n <= 0)
	{
		strcpy (dir, ".");
		return dir;
	}
	dir[n] = 0;

	slash = strrchr (dir, '/');
	if (slash && slash != dir)
		*slash = 0;
	else if (slash)
		slash[1] = 0;

	return dir;
}

/*****************************************************************************/

void Sys_AppActivate (void)
{
}

void Sys_SendKeyEvents (void)
{
#ifndef DEDICATED_ONLY
	if (KBD_Update_fp)
		KBD_Update_fp();
#endif

	// grab frame time 
	sys_frame_time = Sys_Milliseconds();
}

/*****************************************************************************/

char *Sys_GetClipboardData(void)
{
	return NULL;
}

/*
================
Crash reporting.

The engine's stdout is a pipe under docker, so the C library block-buffers it
and a SIGSEGV takes the whole startup log with it: a container that died
during Qcommon_Init would print nothing between the entrypoint's "running"
line and the shell's "Segmentation fault". That is what the setvbuf in main
is for.

And there was no handler, so there was no backtrace either. This one is
written with write() and no formatting library, because a signal handler may
not call printf: backtrace_symbols_fd is the variant that does not allocate.
Frame names need -rdynamic and an unstripped binary.

It re-raises rather than exiting, so the exit status is still 139 and the
entrypoint's restart logic reads the signal as it always did.
================
*/
static void Sys_WriteStr (const char *s)
{
	size_t	n = strlen (s);

	if (write (2, s, n) < 0)
		return;					// nothing useful to do about it in here
}

static void Sys_WriteHex (unsigned long v)
{
	char	buf[2 + sizeof(v) * 2];
	int		i;

	buf[0] = '0';
	buf[1] = 'x';
	for (i = 0; i < (int)sizeof(v) * 2; i++)
		buf[2 + i] = "0123456789abcdef"[(v >> ((sizeof(v) * 2 - 1 - i) * 4)) & 15];

	if (write (2, buf, sizeof(buf)) < 0)
		return;
}

static void Sys_CrashHandler (int sig, siginfo_t *info, void *ucontext)
{
	void	*frames[32];
	int		n;

	Sys_WriteStr ("\n=== Quake II died on signal ");
	switch (sig)
	{
	case SIGSEGV:	Sys_WriteStr ("SIGSEGV (bad address)");		break;
	case SIGBUS:	Sys_WriteStr ("SIGBUS (bad alignment)");	break;
	case SIGFPE:	Sys_WriteStr ("SIGFPE (arithmetic)");		break;
	case SIGILL:	Sys_WriteStr ("SIGILL (illegal instruction)"); break;
	case SIGABRT:	Sys_WriteStr ("SIGABRT (aborted)");			break;
	default:		Sys_WriteStr ("an unexpected signal");		break;
	}

	if (info && (sig == SIGSEGV || sig == SIGBUS))
	{
		Sys_WriteStr (" at ");
		Sys_WriteHex ((unsigned long)(uintptr_t)info->si_addr);
	}
	Sys_WriteStr (" ===\n");

	n = backtrace (frames, (int)(sizeof(frames) / sizeof(frames[0])));
	backtrace_symbols_fd (frames, n, 2);

	Sys_WriteStr ("=== please include the lines above in a bug report ===\n");

	signal (sig, SIG_DFL);
	raise (sig);
}

/*
================
Sys_SignalQuit

SIGTERM is how docker stop, and the entrypoint, ask the engine to stop. The
original had no handler, so the process died where it stood and the
config.cfg that CL_Shutdown writes on the way out was never written: every
setting changed since the last menu quit was lost. This only raises a flag;
the shutdown itself happens in the frame loop, where stdio and Xlib may be
called.
================
*/
static void Sys_SignalQuit (int sig)
{
	sys_signalquit = sig;
}

static void Sys_InitSignals (void)
{
	struct sigaction	sa;
	int					i;
	static const int	crash[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
	static const int	quit[] = { SIGTERM, SIGINT, SIGHUP };

	memset (&sa, 0, sizeof(sa));
	sa.sa_sigaction = Sys_CrashHandler;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	for (i = 0; i < (int)(sizeof(crash) / sizeof(crash[0])); i++)
		sigaction (crash[i], &sa, 0);

	memset (&sa, 0, sizeof(sa));
	sa.sa_handler = Sys_SignalQuit;
	sigemptyset (&sa.sa_mask);
	for (i = 0; i < (int)(sizeof(quit) / sizeof(quit[0])); i++)
		sigaction (quit[i], &sa, 0);

	// A write to a socket or the sound pipe whose reader has gone is an
	// error to handle where it happens, not a reason to die.
	signal (SIGPIPE, SIG_IGN);
}

int main (int argc, char **argv)
{
	int 	time, oldtime, newtime;

	// A line at a time: see the note on crash reporting above.
	setvbuf (stdout, NULL, _IOLBF, 0);

	Sys_InitSignals ();

	saved_euid = geteuid();

	Qcommon_Init(argc, argv);

	fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);

	nostdout = Cvar_Get("nostdout", "0", 0);
	if (!nostdout->value) {
		fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);
	}

	oldtime = Sys_Milliseconds ();
	while (1)
	{
		// The original spun here until the clock moved on a millisecond, and
		// Qcommon_Frame returns without doing anything until a frame is due,
		// so the loop pinned a core at 100% whether anything was happening or
		// not. On a desktop in 1997 that was the whole machine anyway; in a
		// container it is a shared host's. Sleeping out the rest of the
		// millisecond keeps the same granularity for none of the cost.
		newtime = Sys_Milliseconds ();
		time = newtime - oldtime;
		while (time < 1)
		{
			struct timespec	ts = { 0, 500000 };

			nanosleep (&ts, NULL);
			newtime = Sys_Milliseconds ();
			time = newtime - oldtime;
		}

		Qcommon_Frame (time);
		oldtime = newtime;

		if (sys_signalquit)
		{
			printf ("\nReceived signal %d, shutting down\n", (int)sys_signalquit);
			Com_Quit ();
		}
	}

}

/*
================
Sys_CopyProtect

The CD check, which the release compiled out (COPYPROTECT is never defined).
Left as a stub: nobody has the drive any more, and it is the game data, not
the disc, that this port reads.
================
*/
void Sys_CopyProtect(void)
{
}

#if 0
/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{

	int r;
	unsigned long addr;
	int psize = getpagesize();

	addr = (startaddr & ~(psize-1)) - psize;

//	fprintf(stderr, "writable code %lx(%lx)-%lx, length=%lx\n", startaddr,
//			addr, startaddr+length, length);

	r = mprotect((char*)addr, length + startaddr - addr + psize, 7);

	if (r < 0)
    		Sys_Error("Protection change failed\n");

}

#endif
