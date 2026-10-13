/*
** RW_X11.C
**
** This file contains ALL Linux specific stuff having to do with the
** software refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** SWimp_EndFrame
** SWimp_Init
** SWimp_InitGraphics
** SWimp_SetPalette
** SWimp_Shutdown
** SWimp_SwitchFullscreen
**
** Largely rewritten for a current X server; see PORTING-NOTES.md. In short:
**
** The renderer draws palette indices into a buffer of its own, and each
** frame is translated through the palette into an XImage of whatever depth
** the server has -- 24 (the usual, and the container's), 16 or 8. id's
** version drew straight into the XImage and expanded it in place to 16 bits,
** which was the only deeper visual it knew; at depth 24 every frame came out
** as noise.
**
** In the container (vid_resizescreen 1) the window is the whole X screen,
** and a change of video mode resizes the screen itself through RANDR, so the
** picture the browser sees is always exactly the frame. On a desktop the
** window is one among others: the picture is scaled into it, as large as
** fits, whatever size the player or the window manager makes it, and
** fullscreen is the window manager's _NET_WM_STATE_FULLSCREEN.
*/

#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xrandr.h>

#include "../ref_soft/r_local.h"
#include "../client/keys.h"
#include "../linux/rw_linux.h"

/*****************************************************************************/

static Display			*x_disp;
static Colormap			x_cmap;
static Window			x_win;
static GC				x_gc;
static Visual			*x_vis;
static XVisualInfo		*x_visinfo;
static Atom				x_wm_delete_window;
static Cursor			x_nullcursor;

#define STD_EVENT_MASK (StructureNotifyMask | KeyPressMask \
	     | KeyReleaseMask | ExposureMask | PointerMotionMask | \
	     ButtonPressMask | ButtonReleaseMask | FocusChangeMask)

static qboolean			doShm;
static int				x_shmeventtype;
static qboolean			oktodraw = false;
static qboolean			X11_active = false;

// The renderer's frame: palette indices, vid.width by vid.height.
static byte				*x_pixels;

// What goes to the window: the frame through the palette, at the window's
// size. Shared with the server where it can be.
static XImage			*x_image;
static XShmSegmentInfo	x_shminfo;
static qboolean			x_shared;

static int				x_winw, x_winh;		// the window, as last configured
static int				x_outx, x_outy, x_outw, x_outh;	// the frame, in it
static int				*x_outcols;			// the frame's column for each one shown
static qboolean			x_scaler_stale = true;

static qboolean			x_own_screen;		// vid_resizescreen: nothing else is
											// on this display
static qboolean			x_fullscreen;
static qboolean			x_focused = true;
static qboolean			x_grabbed;

static qboolean			x_randr;
static int				x_randr_event;
static int				x_error_seen;
static qboolean			x_resize_warned;

static unsigned int		x_8to32[256];		// the palette, as the server's pixels
static unsigned short	x_8to16[256];

struct
{
	int key;
	int down;
} keyq[64];
int keyq_head=0;
int keyq_tail=0;

static void		VID_InitRandr (void);
static qboolean	VID_SetScreenSize (int width, int height);
static void		VID_RootSize (int *width, int *height);

/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

// this is inside the renderer shared lib, so these are called from vid_so

static qboolean	mouse_avail;
static int		mouse_buttonstate;
static int		mouse_oldbuttonstate;
static int		mouse_x, mouse_y;
static int		old_mouse_x, old_mouse_y;
static int		mx, my;
static int		p_mouse_x, p_mouse_y;

static cvar_t	*_windowed_mouse;
static cvar_t	*m_filter;
static cvar_t	*in_mouse;

static qboolean	mlooking;

// state struct passed in Init
static in_state_t	*in_state;

static cvar_t *sensitivity;
static cvar_t *lookstrafe;
static cvar_t *m_side;
static cvar_t *m_yaw;
static cvar_t *m_pitch;
static cvar_t *m_forward;
static cvar_t *freelook;

// Whether the game, rather than a menu or the console, has the keyboard.
static qboolean RW_InGame (void)
{
	return in_state && in_state->key_dest && *in_state->key_dest == 0;
}

static void Force_CenterView_f (void)
{
	in_state->viewangles[PITCH] = 0;
}

static void RW_IN_MLookDown (void)
{
	mlooking = true;
}

static void RW_IN_MLookUp (void)
{
	mlooking = false;
	in_state->IN_CenterView_fp ();
}

void RW_IN_Init(in_state_t *in_state_p)
{
	in_state = in_state_p;

	// mouse variables
	_windowed_mouse = ri.Cvar_Get ("_windowed_mouse", "0", CVAR_ARCHIVE);
	m_filter = ri.Cvar_Get ("m_filter", "0", 0);
	in_mouse = ri.Cvar_Get ("in_mouse", "1", CVAR_ARCHIVE);
	freelook = ri.Cvar_Get( "freelook", "0", 0 );
	lookstrafe = ri.Cvar_Get ("lookstrafe", "0", 0);
	sensitivity = ri.Cvar_Get ("sensitivity", "3", 0);
	m_pitch = ri.Cvar_Get ("m_pitch", "0.022", 0);
	m_yaw = ri.Cvar_Get ("m_yaw", "0.022", 0);
	m_forward = ri.Cvar_Get ("m_forward", "1", 0);
	m_side = ri.Cvar_Get ("m_side", "0.8", 0);

	ri.Cmd_AddCommand ("+mlook", RW_IN_MLookDown);
	ri.Cmd_AddCommand ("-mlook", RW_IN_MLookUp);

	ri.Cmd_AddCommand ("force_centerview", Force_CenterView_f);

	mouse_x = mouse_y = 0;
	mx = my = 0;
	mouse_avail = true;
}

void RW_IN_Shutdown(void)
{
	if (mouse_avail)
	{
		ri.Cmd_RemoveCommand ("+mlook");
		ri.Cmd_RemoveCommand ("-mlook");
		ri.Cmd_RemoveCommand ("force_centerview");
	}
	mouse_avail = false;
}

/*
===========
IN_Commands
===========
*/
void RW_IN_Commands (void)
{
	int i;

	if (!mouse_avail)
		return;

	for (i=0 ; i<3 ; i++) {
		if ( (mouse_buttonstate & (1<<i)) && !(mouse_oldbuttonstate & (1<<i)) )
			in_state->Key_Event_fp (K_MOUSE1 + i, true);

		if ( !(mouse_buttonstate & (1<<i)) && (mouse_oldbuttonstate & (1<<i)) )
			in_state->Key_Event_fp (K_MOUSE1 + i, false);
	}
	mouse_oldbuttonstate = mouse_buttonstate;
}

/*
===========
IN_Move
===========
*/
void RW_IN_Move (usercmd_t *cmd)
{
	if (!mouse_avail || !in_mouse->value)
		return;

	// Movement made in a menu or the console is not the player turning; the
	// original kept it and spent it all at once on the way back to the game.
	if (!RW_InGame ())
	{
		mx = my = 0;
		return;
	}

	if (m_filter->value)
	{
		mouse_x = (mx + old_mouse_x) * 0.5;
		mouse_y = (my + old_mouse_y) * 0.5;
	} else {
		mouse_x = mx;
		mouse_y = my;
	}

	old_mouse_x = mx;
	old_mouse_y = my;
	mx = my = 0;

	if (!mouse_x && !mouse_y)
		return;

	mouse_x *= sensitivity->value;
	mouse_y *= sensitivity->value;

// add mouse X/Y movement to cmd
	if ( (*in_state->in_strafe_state & 1) ||
		(lookstrafe->value && mlooking ))
		cmd->sidemove += m_side->value * mouse_x;
	else
		in_state->viewangles[YAW] -= m_yaw->value * mouse_x;

	if ( (mlooking || freelook->value) &&
		!(*in_state->in_strafe_state & 1))
	{
		in_state->viewangles[PITCH] += m_pitch->value * mouse_y;
	}
	else
	{
		cmd->forwardmove -= m_forward->value * mouse_y;
	}
}

void RW_IN_Frame (void)
{
}

void RW_IN_Activate(qboolean active)
{
}

/*****************************************************************************/
/* PIXELS                                                                    */
/*****************************************************************************/

/*
================
X_PixelFor

A colour as the visual's pixel value, from its channel masks.
================
*/
static unsigned int X_Channel (int v, unsigned long mask)
{
	int		shift = 0, bits = 0;

	if (!mask)
		return 0;
	while (!(mask & 1))
	{
		mask >>= 1;
		shift++;
	}
	while (mask & 1)
	{
		mask >>= 1;
		bits++;
	}
	if (bits < 8)
		v >>= 8 - bits;
	else
		v <<= bits - 8;
	return (unsigned int)v << shift;
}

static unsigned int X_PixelFor (int r, int g, int b)
{
	return X_Channel (r, x_vis->red_mask)
		 | X_Channel (g, x_vis->green_mask)
		 | X_Channel (b, x_vis->blue_mask);
}

// ========================================================================
// makes a null cursor
// ========================================================================

static Cursor CreateNullCursor(Display *display, Window root)
{
    Pixmap cursormask;
    XGCValues xgc;
    GC gc;
    XColor dummycolour;
    Cursor cursor;

    cursormask = XCreatePixmap(display, root, 1, 1, 1/*depth*/);
    xgc.function = GXclear;
    gc =  XCreateGC(display, cursormask, GCFunction, &xgc);
    XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
    dummycolour.pixel = 0;
    dummycolour.red = 0;
    dummycolour.flags = 04;
    cursor = XCreatePixmapCursor(display, cursormask, cursormask,
          &dummycolour,&dummycolour, 0,0);
    XFreePixmap(display,cursormask);
    XFreeGC(display,gc);
    return cursor;
}

/*
================
X_FreeImage
================
*/
static void X_FreeImage (void)
{
	if (!x_image)
		return;

	if (x_shared)
	{
		XShmDetach (x_disp, &x_shminfo);
		XSync (x_disp, False);
		shmdt (x_shminfo.shmaddr);
		x_image->data = NULL;
	}
	XDestroyImage (x_image);
	x_image = NULL;
	x_shared = false;
}

/*
================
X_AllocImage

The window's picture, at the window's size. Shared memory when the server is
on this machine; the original keyed the segment on random(), which collides
with whatever else on the machine did the same, and IPC_PRIVATE cannot.
================
*/
static void X_AllocImage (int width, int height)
{
	int		size;

	X_FreeImage ();

	if (doShm)
	{
		x_image = XShmCreateImage (x_disp, x_vis, x_visinfo->depth, ZPixmap, 0,
								   &x_shminfo, width, height);
		if (x_image)
		{
			size = x_image->bytes_per_line * x_image->height;
			x_shminfo.shmid = shmget (IPC_PRIVATE, size, IPC_CREAT|0600);
			if (x_shminfo.shmid != -1)
			{
				x_shminfo.shmaddr = (void *)shmat (x_shminfo.shmid, 0, 0);
				if (x_shminfo.shmaddr != (void *)-1)
				{
					x_image->data = x_shminfo.shmaddr;
					x_shminfo.readOnly = False;
					x_error_seen = 0;
					if (XShmAttach (x_disp, &x_shminfo))
					{
						XSync (x_disp, False);
						shmctl (x_shminfo.shmid, IPC_RMID, 0);
						if (!x_error_seen)
						{
							x_shared = true;
							return;
						}
					}
					shmdt (x_shminfo.shmaddr);
				}
				shmctl (x_shminfo.shmid, IPC_RMID, 0);
			}
			x_image->data = NULL;
			XDestroyImage (x_image);
			x_image = NULL;
		}

		// The server said it had the extension and then would not attach:
		// a server in another container, sharing the socket but not the
		// IPC namespace, does exactly that. Plain XPutImage works anywhere.
		ri.Con_Printf (PRINT_ALL, "MITSHM unavailable; sending frames over the socket\n");
		doShm = false;
	}

	x_image = XCreateImage (x_disp, x_vis, x_visinfo->depth, ZPixmap, 0, NULL,
							width, height, 32, 0);
	if (!x_image)
		Sys_Error ("VID: XCreateImage failed for %dx%d\n", width, height);
	size = x_image->bytes_per_line * x_image->height;
	x_image->data = malloc (size);
	if (!x_image->data)
		Sys_Error ("VID: out of memory for a %d byte window image\n", size);
	memset (x_image->data, 0, size);
}

/*
================
X_ResetScaler

After a change to the window's size or the frame's: the window-sized image,
and where in it the frame goes -- as large as fits with its shape kept, and
black around it. When the two are the same size, which is always in the
container, the frame is simply translated pixel for pixel.
================
*/
static void X_ResetScaler (void)
{
	int		i;

	x_scaler_stale = false;

	if (!x_image || x_image->width != x_winw || x_image->height != x_winh)
		X_AllocImage (x_winw, x_winh);
	else
		memset (x_image->data, 0, x_image->bytes_per_line * x_image->height);

	if (x_winw * vid.height > x_winh * vid.width)
	{
		x_outh = x_winh;
		x_outw = vid.width * x_winh / vid.height;
	}
	else
	{
		x_outw = x_winw;
		x_outh = vid.height * x_winw / vid.width;
	}
	if (x_outw < 1)
		x_outw = 1;
	if (x_outh < 1)
		x_outh = 1;
	x_outx = (x_winw - x_outw) / 2;
	x_outy = (x_winh - x_outh) / 2;

	x_outcols = realloc (x_outcols, x_outw * sizeof(*x_outcols));
	if (!x_outcols)
		Sys_Error ("VID: out of memory for the scaler\n");
	for (i = 0 ; i < x_outw ; i++)
		x_outcols[i] = i * vid.width / x_outw;
}

/*
================
X_Present

The frame, through the palette, into the window's image. Nearest pixel when
scaled: the renderer's pixels are the look. A row of the window that shows
the same row of the frame as the one above it is a copy of it.
================
*/
static void X_Present (void)
{
	int		x, y, sy, lastsy, bytes;
	byte	*src, *row, *prev;
	XImage	*out = x_image;
	qboolean	same = (x_outw == vid.width && x_outh == vid.height);

	bytes = out->bits_per_pixel / 8;
	lastsy = -1;
	prev = NULL;

	for (y = 0 ; y < x_outh ; y++)
	{
		sy = same ? y : y * vid.height / x_outh;
		row = (byte *)out->data + (x_outy + y) * out->bytes_per_line
			+ x_outx * bytes;

		if (sy == lastsy)
		{
			memcpy (row, prev, x_outw * bytes);
			prev = row;
			continue;
		}

		src = x_pixels + sy * vid.rowbytes;
		switch (bytes)
		{
		case 4:
			if (same)
				for (x = 0 ; x < x_outw ; x++)
					((unsigned int *)row)[x] = x_8to32[src[x]];
			else
				for (x = 0 ; x < x_outw ; x++)
					((unsigned int *)row)[x] = x_8to32[src[x_outcols[x]]];
			break;
		case 2:
			for (x = 0 ; x < x_outw ; x++)
				((unsigned short *)row)[x] = x_8to16[src[same ? x : x_outcols[x]]];
			break;
		default:
			for (x = 0 ; x < x_outw ; x++)
				row[x] = src[same ? x : x_outcols[x]];
			break;
		}
		prev = row;
		lastsy = sy;
	}
}

/*****************************************************************************/
/* KEYS AND EVENTS                                                           */
/*****************************************************************************/

int XLateKey(XKeyEvent *ev)
{

	int key;
	char buf[64];
	KeySym keysym;
	XKeyEvent unshifted;

	key = 0;

// Look the keysym up with shift taken out of the event's modifier state.
//
// The engine wants the unshifted key: keys.h says "normal keys should be
// passed as lowercased ascii", keys.c owns the shift table that turns '-'
// into '_' for the console, and a binding belongs to a physical key rather
// than the character it happens to produce. Taking the keysym with shift
// applied also made a press and its release disagree -- shift+minus went
// down as '_' and, shift being up by then, came up as '-' -- and Key_Event
// then took every later '_' for an autorepeat of one still held.
	unshifted = *ev;
	unshifted.state &= ~(ShiftMask | LockMask);

	XLookupString(&unshifted, buf, sizeof buf, &keysym, 0);

	switch(keysym)
	{
		case XK_KP_Page_Up:	 key = K_KP_PGUP; break;
		case XK_Page_Up:	 key = K_PGUP; break;

		case XK_KP_Page_Down: key = K_KP_PGDN; break;
		case XK_Page_Down:	 key = K_PGDN; break;

		case XK_KP_Home: key = K_KP_HOME; break;
		case XK_Home:	 key = K_HOME; break;

		case XK_KP_End:  key = K_KP_END; break;
		case XK_End:	 key = K_END; break;

		case XK_KP_Left: key = K_KP_LEFTARROW; break;
		case XK_Left:	 key = K_LEFTARROW; break;

		case XK_KP_Right: key = K_KP_RIGHTARROW; break;
		case XK_Right:	key = K_RIGHTARROW;		break;

		case XK_KP_Down: key = K_KP_DOWNARROW; break;
		case XK_Down:	 key = K_DOWNARROW; break;

		case XK_KP_Up:   key = K_KP_UPARROW; break;
		case XK_Up:		 key = K_UPARROW;	 break;

		case XK_Escape: key = K_ESCAPE;		break;

		case XK_KP_Enter: key = K_KP_ENTER;	break;
		case XK_Return: key = K_ENTER;		 break;

		case XK_Tab:		key = K_TAB;			 break;

		case XK_F1:		 key = K_F1;				break;
		case XK_F2:		 key = K_F2;				break;
		case XK_F3:		 key = K_F3;				break;
		case XK_F4:		 key = K_F4;				break;
		case XK_F5:		 key = K_F5;				break;
		case XK_F6:		 key = K_F6;				break;
		case XK_F7:		 key = K_F7;				break;
		case XK_F8:		 key = K_F8;				break;
		case XK_F9:		 key = K_F9;				break;
		case XK_F10:		key = K_F10;			 break;
		case XK_F11:		key = K_F11;			 break;
		case XK_F12:		key = K_F12;			 break;

		case XK_BackSpace: key = K_BACKSPACE; break;

		case XK_KP_Delete: key = K_KP_DEL; break;
		case XK_Delete: key = K_DEL; break;

		case XK_Pause:	key = K_PAUSE;		 break;

		case XK_Shift_L:
		case XK_Shift_R:	key = K_SHIFT;		break;

		case XK_Execute:
		case XK_Control_L:
		case XK_Control_R:	key = K_CTRL;		 break;

		case XK_Alt_L:
		case XK_Meta_L:
		case XK_Alt_R:
		case XK_Meta_R: key = K_ALT;			break;

		case XK_KP_Begin: key = K_KP_5;	break;

		case XK_Insert:key = K_INS; break;
		case XK_KP_Insert: key = K_KP_INS; break;

		case XK_KP_Multiply: key = '*'; break;
		case XK_KP_Add:  key = K_KP_PLUS; break;
		case XK_KP_Subtract: key = K_KP_MINUS; break;
		case XK_KP_Divide: key = K_KP_SLASH; break;

		default:
			key = *(unsigned char*)buf;
			if (key >= 'A' && key <= 'Z')
				key = key - 'A' + 'a';
			break;
	}

	return key;
}

/*
================
Keyq_Add

Everything that turns into a key goes through the ring, and KBD_Update hands
it on once a frame. The wheel goes through here too rather than through
mouse_buttonstate: a notch is a press and a release in the same instant, and
IN_Commands only sees what changed between frames, so a notch would cancel
itself out.
================
*/
static void Keyq_Add (int key, qboolean down)
{
	if (!key)
		return;
	keyq[keyq_head].key = key;
	keyq[keyq_head].down = down;
	keyq_head = (keyq_head + 1) & 63;
}

static int X_Button (int button)
{
	switch (button)
	{
	case 1:	return 0;
	case 2:	return 2;
	case 3:	return 1;
	}
	return -1;
}

/*
================
X_UpdateGrab

On a desktop the pointer is held -- grabbed, hidden and kept in the middle of
the window -- only while the game is being played in the window that has the
keyboard. In the menus, the console, or with another window in front, it is
the player's again. _windowed_mouse 0 turns holding it off altogether.

In the container nothing is grabbed: the browser has the real pointer, and
VNC moves this one to wherever it says (see the MotionNotify case).
================
*/
static void X_UpdateGrab (void)
{
	qboolean	want;

	if (x_own_screen || !x_win)
		return;

	want = _windowed_mouse && _windowed_mouse->value && x_focused && RW_InGame ();
	if (want == x_grabbed)
		return;

	if (want)
	{
		if (XGrabPointer (x_disp, x_win, True, 0, GrabModeAsync, GrabModeAsync,
						  x_win, None, CurrentTime) != GrabSuccess)
			return;			// something else has it; try next frame
		XDefineCursor (x_disp, x_win, x_nullcursor);
		XWarpPointer (x_disp, None, x_win, 0, 0, 0, 0, x_winw/2, x_winh/2);
	}
	else
	{
		XUngrabPointer (x_disp, CurrentTime);
		XUndefineCursor (x_disp, x_win);
	}

	x_grabbed = want;
	mx = my = 0;
}

void GetEvent(void)
{
	XEvent x_event;
	int b;

	XNextEvent(x_disp, &x_event);
	switch(x_event.type) {
	case KeyPress:
		Keyq_Add (XLateKey(&x_event.xkey), true);
		break;
	case KeyRelease:
		Keyq_Add (XLateKey(&x_event.xkey), false);
		break;

	case MotionNotify:
		if (x_own_screen)
		{
		// The container. The browser walks this pointer around the screen
		// by sending absolute positions -- that is all VNC carries -- and
		// puts it back in the middle before it would reach an edge. A report
		// landing exactly on the middle is that re-base, not a movement:
		// measured from the last position it would read as a hard flick
		// the other way.
			if (x_event.xmotion.x == x_winw/2 && x_event.xmotion.y == x_winh/2)
			{
				p_mouse_x = x_event.xmotion.x;
				p_mouse_y = x_event.xmotion.y;
				break;
			}
		// Added up, not assigned as the original did: several reports
		// arrive between two frames, and all but the last were lost.
			mx += x_event.xmotion.x - p_mouse_x;
			my += x_event.xmotion.y - p_mouse_y;
			p_mouse_x = x_event.xmotion.x;
			p_mouse_y = x_event.xmotion.y;
			break;
		}

		if (!x_grabbed)
		{
		// A desktop pointer that is not held for play is the player's, to
		// take to another window; moving it turns nothing.
			p_mouse_x = x_event.xmotion.x;
			p_mouse_y = x_event.xmotion.y;
			break;
		}

		if (x_event.xmotion.x == x_winw/2 && x_event.xmotion.y == x_winh/2)
			break;		// the warp below coming back
		mx += x_event.xmotion.x - x_winw/2;
		my += x_event.xmotion.y - x_winh/2;

		/* move the mouse to the window center again */
		XSelectInput(x_disp,x_win, STD_EVENT_MASK & ~PointerMotionMask);
		XWarpPointer(x_disp,None,x_win,0,0,0,0, x_winw/2, x_winh/2);
		XSelectInput(x_disp,x_win, STD_EVENT_MASK);
		break;

	case ButtonPress:
		if (x_event.xbutton.button == 4)
		{
			Keyq_Add (K_MWHEELUP, true);
			break;
		}
		if (x_event.xbutton.button == 5)
		{
			Keyq_Add (K_MWHEELDOWN, true);
			break;
		}
		b = X_Button (x_event.xbutton.button);
		if (b>=0)
			mouse_buttonstate |= 1<<b;
		break;

	case ButtonRelease:
		if (x_event.xbutton.button == 4)
		{
			Keyq_Add (K_MWHEELUP, false);
			break;
		}
		if (x_event.xbutton.button == 5)
		{
			Keyq_Add (K_MWHEELDOWN, false);
			break;
		}
		b = X_Button (x_event.xbutton.button);
		if (b>=0)
			mouse_buttonstate &= ~(1<<b);
		break;

	case ConfigureNotify:
		if (x_event.xconfigure.window != x_win)
			break;
		if (x_event.xconfigure.width != x_winw || x_event.xconfigure.height != x_winh)
		{
			x_winw = x_event.xconfigure.width;
			x_winh = x_event.xconfigure.height;
			x_scaler_stale = true;
		}
		break;

	case FocusIn:
	case FocusOut:
	// Grabbing and letting go of the pointer send these as well; they are
	// not the player moving to another window.
		if (x_event.xfocus.mode == NotifyGrab
			|| x_event.xfocus.mode == NotifyUngrab)
			break;
		x_focused = x_event.type == FocusIn;
		break;

	case MappingNotify:
	// Xlib caches the keyboard mapping when the connection opens and only
	// rereads it when asked. x11vnc types a character the keymap does not
	// have by binding it to a spare keycode, sending it and putting the
	// keymap back, so without this those characters arrive as whatever the
	// stale cache says that keycode used to mean.
		XRefreshKeyboardMapping (&x_event.xmapping);
		break;

	case ClientMessage:
	// Closing the window ended the process from inside Xlib's I/O error
	// handler, with the connection already gone, so no config was written.
	// Quit through the engine instead.
		if ((Atom)x_event.xclient.data.l[0] == x_wm_delete_window)
			ri.Cmd_ExecuteText (EXEC_APPEND, "quit\n");
		break;

	default:
		if (x_randr && x_event.type == x_randr_event + RRScreenChangeNotify)
		{
			XRRUpdateConfiguration (&x_event);
			break;
		}
		if (doShm && x_event.type == x_shmeventtype)
			oktodraw = true;
	}
}

/*****************************************************************************/
/* SCREEN                                                                    */
/*****************************************************************************/

/*
================
VID_XErrorTrap

RANDR answers a mode larger than the screen maximum with BadMatch, and a
server that cannot see our shared memory answers XShmAttach with BadAccess.
Xlib's default handler prints those and calls exit(), which would end the game
over a menu selection. Count them and carry on; the callers check what
actually happened rather than trusting the request.
================
*/
static int VID_XErrorTrap (Display *disp, XErrorEvent *err)
{
	x_error_seen++;
	return 0;
}

/*
================
VID_RootSize

Asks the server. DisplayWidth/DisplayHeight read a value Xlib cached when the
connection opened, and it does not follow a screen resize.
================
*/
static void VID_RootSize (int *width, int *height)
{
	Window			root, dummy;
	int				x, y;
	unsigned int	w, h, border, depth;

	root = XDefaultRootWindow (x_disp);
	if (XGetGeometry (x_disp, root, &dummy, &x, &y, &w, &h, &border, &depth))
	{
		*width = w;
		*height = h;
	}
	else
	{
		*width = vid.width;
		*height = vid.height;
	}
}

/*
================
VID_InitRandr
================
*/
static void VID_InitRandr (void)
{
	int		error_base, major, minor;

	x_randr = false;

	if (!XRRQueryExtension (x_disp, &x_randr_event, &error_base)
		|| !XRRQueryVersion (x_disp, &major, &minor))
		return;

// XRRSetCrtcConfig and the mode-creation calls are 1.2. The 1.1 interface
// reports success on Xvfb and leaves the screen where it was.
	if (major < 1 || (major == 1 && minor < 2))
		return;

	x_randr = true;
	XRRSelectInput (x_disp, XDefaultRootWindow (x_disp), RRScreenChangeNotifyMask);
}

/*
================
VID_SetPhysSize

RRSetScreenSize wants a physical size, and a zero makes every client that
computes a DPI divide by it. 96 dpi.
================
*/
static void VID_SetPhysSize (Window root, int width, int height)
{
	XRRSetScreenSize (x_disp, root, width, height,
					  (width * 254) / 960, (height * 254) / 960);
}

/*
================
VID_SetScreenSize

The container's X server is a private Xvfb with one window on it, and the
picture the browser sees is the whole root window, so a change of resolution
has to change the size of the screen and not only the window. A server's
maximum screen size is fixed when it starts, so the entrypoint starts Xvfb at
the largest mode the renderer can draw, and Xvfb starts knowing exactly one
mode, so every other size is created here before a CRTC will take it.

Returns whether the screen is now the size asked for.
================
*/
static qboolean VID_SetScreenSize (int width, int height)
{
	XRRScreenResources	*res;
	XRROutputInfo		*output = NULL;
	XRRModeInfo			mode_info;
	char				mode_name[32];
	RRMode				mode = None;
	RROutput			out = None;
	RRCrtc				crtc = None;
	Window				root;
	int					i, cur_width, cur_height;

	if (!x_randr || !x_own_screen)
		return false;

	VID_RootSize (&cur_width, &cur_height);
	if (cur_width == width && cur_height == height)
		return true;

	root = XDefaultRootWindow (x_disp);

	res = XRRGetScreenResources (x_disp, root);
	if (!res)
		return false;

	for (i = 0 ; i < res->noutput ; i++)
	{
		output = XRRGetOutputInfo (x_disp, res, res->outputs[i]);
		if (output && output->connection == RR_Connected)
		{
			out = res->outputs[i];
			crtc = output->crtc ? output->crtc
								: (output->ncrtc ? output->crtcs[0] : None);
			break;
		}
		if (output)
		{
			XRRFreeOutputInfo (output);
			output = NULL;
		}
	}

	if (out == None || crtc == None)
	{
		if (output)
			XRRFreeOutputInfo (output);
		XRRFreeScreenResources (res);
		return false;
	}

	for (i = 0 ; i < res->nmode ; i++)
		if (res->modes[i].width == width && res->modes[i].height == height)
		{
			mode = res->modes[i].id;
			break;
		}

	x_error_seen = 0;

	if (mode == None)
	{
	// The timings are never used -- nothing here drives a pixel clock -- but
	// the server rejects a mode whose totals do not bound its visible area.
		sprintf (mode_name, "%dx%d", width, height);
		memset (&mode_info, 0, sizeof(mode_info));
		mode_info.name = mode_name;
		mode_info.nameLength = strlen (mode_name);
		mode_info.width = width;
		mode_info.height = height;
		mode_info.hSyncStart = width + 8;
		mode_info.hSyncEnd = width + 40;
		mode_info.hTotal = width + 80;
		mode_info.vSyncStart = height + 3;
		mode_info.vSyncEnd = height + 9;
		mode_info.vTotal = height + 20;
		mode_info.dotClock = (unsigned long)mode_info.hTotal
							 * mode_info.vTotal * 60;

		mode = XRRCreateMode (x_disp, root, &mode_info);
		if (mode != None)
			XRRAddOutputMode (x_disp, out, mode);
		XSync (x_disp, False);
	}
	else
	{
		for (i = 0 ; i < output->nmode ; i++)
			if (output->modes[i] == mode)
				break;
		if (i == output->nmode)
		{
			XRRAddOutputMode (x_disp, out, mode);
			XSync (x_disp, False);
		}
	}

	if (mode != None && !x_error_seen)
	{
	// Growing: the screen has to be large enough before the CRTC will take
	// the mode. Shrinking: the CRTC has to come down first or the screen
	// would no longer cover it. Doing both, in that order, covers either.
		if (width > cur_width || height > cur_height)
		{
			VID_SetPhysSize (root,
							 width > cur_width ? width : cur_width,
							 height > cur_height ? height : cur_height);
			XSync (x_disp, False);
		}

		XRRSetCrtcConfig (x_disp, res, crtc, CurrentTime, 0, 0,
						  mode, RR_Rotate_0, &out, 1);
		VID_SetPhysSize (root, width, height);
		XSync (x_disp, False);
	}

	XRRFreeOutputInfo (output);
	XRRFreeScreenResources (res);

	VID_RootSize (&cur_width, &cur_height);
	return cur_width == width && cur_height == height;
}

/*
================
VID_HaveWM

Whether a window manager that follows the freedesktop conventions is running.
Without one, nothing will act on a request to go fullscreen.
================
*/
static qboolean VID_HaveWM (void)
{
	Atom			check, type;
	int				format;
	unsigned long	count, after;
	unsigned char	*data = NULL;
	qboolean		have;

	check = XInternAtom (x_disp, "_NET_SUPPORTING_WM_CHECK", False);
	if (XGetWindowProperty (x_disp, XDefaultRootWindow (x_disp), check, 0, 1,
							False, XA_WINDOW, &type, &format, &count, &after,
							&data) != Success)
		return false;

	have = data && count == 1;
	if (data)
		XFree (data);
	return have;
}

/*
================
VID_ApplyFullscreen

Asks the window manager to make the window cover its monitor, or to put it
back. It answers with a ConfigureNotify, and the frame is scaled to whatever
size that says. With no window manager the window is made the size of the
screen.
================
*/
static void VID_ApplyFullscreen (qboolean on)
{
	XEvent	ev;
	int		w, h;

	if (on == x_fullscreen)
		return;
	x_fullscreen = on;

	if (VID_HaveWM ())
	{
		memset (&ev, 0, sizeof(ev));
		ev.type = ClientMessage;
		ev.xclient.window = x_win;
		ev.xclient.message_type = XInternAtom (x_disp, "_NET_WM_STATE", False);
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = on ? 1 : 0;		// _NET_WM_STATE_ADD / _REMOVE
		ev.xclient.data.l[1] = XInternAtom (x_disp, "_NET_WM_STATE_FULLSCREEN",
											False);
		ev.xclient.data.l[3] = 1;				// from an application
		XSendEvent (x_disp, XDefaultRootWindow (x_disp), False,
					SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	}
	else if (on)
	{
		VID_RootSize (&w, &h);
		XMoveResizeWindow (x_disp, x_win, 0, 0, w, h);
	}

	if (!on)
		XResizeWindow (x_disp, x_win, vid.width, vid.height);

	XSync (x_disp, False);
}

/*****************************************************************************/

/*
** SWimp_Init
**
** This routine is responsible for initializing the implementation
** specific stuff in a software rendering subsystem.
*/
int SWimp_Init( void *hInstance, void *wndProc )
{
	cvar_t	*resizescreen;

// open the display
	x_disp = XOpenDisplay(0);
	if (!x_disp)
	{
		if (getenv("DISPLAY"))
			Sys_Error("VID: Could not open display [%s]\n",
				getenv("DISPLAY"));
		else
			Sys_Error("VID: Could not open local display\n");
	}

// id caught SIGINT and SIGTERM here, to turn the server's key repeat back on
// before calling Sys_Error -- from inside the handler, where neither Xlib nor
// stdio may be called. sys_linux.c's handler raises a flag instead and the
// engine quits normally; SWimp_Shutdown restores the repeat on the way out.

	resizescreen = ri.Cvar_Get ("vid_resizescreen", "0", CVAR_NOSET);
	x_own_screen = resizescreen->value != 0;

// Key repeat off, so that a held key is one press. id turned it off for the
// whole X server, which is harmless on the container's private one and not
// on a desktop, where every other program loses its key repeat until the game
// exits -- or for good, if it crashes. There, ask for repeats that can be
// told apart instead, and Key_Event already ignores a press of a key that is
// down.
	if (x_own_screen)
		XAutoRepeatOff(x_disp);
	else
		XkbSetDetectableAutoRepeat (x_disp, True, NULL);

	XSetErrorHandler (VID_XErrorTrap);

	VID_InitRandr ();

	return true;
}

/*
** X_CreateWindow
**
** The window, once; a change of mode resizes it rather than making another.
*/
static void X_CreateWindow (void)
{
	XVisualInfo template;
	int num_visuals;
	int screen;

	screen = XDefaultScreen(x_disp);
	template.visualid = XVisualIDFromVisual(XDefaultVisual(x_disp, screen));
	x_visinfo = XGetVisualInfo(x_disp, VisualIDMask, &template, &num_visuals);
	if (num_visuals == 0)
		Sys_Error("VID: Bad visual id %d\n", (int)template.visualid);

	x_vis = x_visinfo->visual;

	if (x_visinfo->depth != 8 && x_visinfo->depth != 15 && x_visinfo->depth != 16
		&& x_visinfo->depth != 24 && x_visinfo->depth != 32)
		Sys_Error ("VID: a depth %d visual is not one this renderer can draw to\n",
				   x_visinfo->depth);

	if (x_visinfo->depth == 8 && x_visinfo->class != PseudoColor)
		Sys_Error ("VID: the 8-bit visual is not PseudoColor, so the palette\n"
				   "cannot be set. Use a server at depth 24.\n");

// setup attributes for main window
	{
	   int attribmask = CWEventMask  | CWColormap | CWBorderPixel | CWBackPixel;
	   XSetWindowAttributes attribs;

	   if (x_visinfo->depth == 8)
		   x_cmap = XCreateColormap(x_disp, XRootWindow(x_disp, x_visinfo->screen),
									x_vis, AllocAll);
	   else
		   x_cmap = XCreateColormap(x_disp, XRootWindow(x_disp, x_visinfo->screen),
									x_vis, AllocNone);

	   attribs.event_mask = STD_EVENT_MASK;
	   attribs.border_pixel = 0;
	   attribs.background_pixel = 0;
	   attribs.colormap = x_cmap;

		x_win = XCreateWindow(	x_disp,
			XRootWindow(x_disp, x_visinfo->screen),
			0, 0,	// x, y
			vid.width, vid.height,
			0, // borderwidth
			x_visinfo->depth,
			InputOutput,
			x_vis,
			attribmask,
			&attribs );
	}

	x_winw = vid.width;
	x_winh = vid.height;

// The container's tools find the window by this name. On a desktop it is the
// title bar and the task bar, and the class is what a .desktop file's
// StartupWMClass matches.
	{
		XClassHint	hint;

		XStoreName (x_disp, x_win, x_own_screen ? "quake2" : "Quake II");
		hint.res_name = "quake2";
		hint.res_class = "Quake2";
		XSetClassHint (x_disp, x_win, &hint);
	}

	if (x_visinfo->depth == 8)
	{
	// Setting the window's colormap only states a preference; installing it
	// is the window manager's job, and the container has none.
		XInstallColormap(x_disp, x_cmap);
	}

// Closing the window should reach the engine rather than killing the X
// connection underneath it.
	x_wm_delete_window = XInternAtom (x_disp, "WM_DELETE_WINDOW", False);
	XSetWMProtocols (x_disp, x_win, &x_wm_delete_window, 1);

// inviso cursor: always in the container, where the browser draws its own;
// on a desktop only while the pointer is held for play (X_UpdateGrab)
	x_nullcursor = CreateNullCursor(x_disp, x_win);
	if (x_own_screen)
		XDefineCursor(x_disp, x_win, x_nullcursor);

// create the GC
	{
		XGCValues xgcvalues;
		int valuemask = GCGraphicsExposures;
		xgcvalues.graphics_exposures = False;
		x_gc = XCreateGC(x_disp, x_win, valuemask, &xgcvalues );
	}

// map the window
	XMapWindow(x_disp, x_win);

// wait for first exposure event
	{
		XEvent event;
		do
		{
			XNextEvent(x_disp, &event);
			if (event.type == Expose && !event.xexpose.count)
				oktodraw = true;
		} while (!oktodraw);
	}

// MITSHM only for a server on this machine. The original found the host part
// of $DISPLAY by writing a nul over its colon -- in the string getenv handed
// back, which is the environment itself.
	doShm = false;
	if (XShmQueryExtension(x_disp))
	{
		const char	*displayname = getenv("DISPLAY");
		char		host[64];
		size_t		n = 0;

		doShm = true;
		if (displayname)
		{
			while (displayname[n] && displayname[n] != ':' && n < sizeof(host) - 1)
			{
				host[n] = displayname[n];
				n++;
			}
			host[n] = 0;
			if (!(!strcasecmp(host, "unix") || !host[0]))
				doShm = false;
		}
	}
	if (doShm)
		x_shmeventtype = XShmGetEventBase(x_disp) + ShmCompletion;

	ri.Con_Printf (PRINT_ALL, "X11: depth %d visual, %s\n", x_visinfo->depth,
				   doShm ? "shared memory" : "over the socket");
}

/*
** SWimp_InitGraphics
**
** This initializes the software refresh's implementation specific
** graphics subsystem.
**
** The necessary width and height parameters are grabbed from
** vid.width and vid.height.
*/
static qboolean SWimp_InitGraphics( qboolean fullscreen )
{
	int		rw, rh;

	// let the sound and input subsystems know about the new window
	ri.Vid_NewWindow (vid.width, vid.height);

	// The container: the screen is the window, so it is made the frame's
	// size before the window is. If the server will not resize, take the
	// screen as it stands, since a window that does not fill it leaves the
	// rest of the picture black.
	if (x_own_screen && !VID_SetScreenSize (vid.width, vid.height))
	{
		VID_RootSize (&rw, &rh);
		if ((rw != vid.width || rh != vid.height) && !x_resize_warned)
		{
			x_resize_warned = true;
			ri.Con_Printf (PRINT_ALL, "VID: this X server will not resize its screen to"
						   " %dx%d;\n     the picture is scaled to its %dx%d.\n",
						   vid.width, vid.height, rw, rh);
		}
	}

	if (!x_win)
		X_CreateWindow ();
	else if (!x_fullscreen || x_own_screen)
		XResizeWindow (x_disp, x_win, vid.width, vid.height);

	if (x_own_screen)
	{
		VID_RootSize (&rw, &rh);
		XMoveResizeWindow (x_disp, x_win, 0, 0, rw, rh);
		x_winw = rw;
		x_winh = rh;
	}
	else
	{
		VID_ApplyFullscreen (fullscreen);
		if (!x_fullscreen)
		{
			x_winw = vid.width;
			x_winh = vid.height;
		}
	}
	XSync (x_disp, False);

	// The renderer's own frame. vid.rowbytes is the frame's width: the
	// original drew into the XImage, whose rows are four bytes a pixel at
	// depth 24, and left three quarters of every row unused.
	free (x_pixels);
	x_pixels = malloc (vid.width * vid.height);
	if (!x_pixels)
		Sys_Error ("VID: out of memory for a %dx%d frame\n", vid.width, vid.height);
	memset (x_pixels, 0, vid.width * vid.height);
	vid.buffer = x_pixels;
	vid.rowbytes = vid.width;

	x_scaler_stale = true;
	X11_active = true;

	return true;
}

/*
** SWimp_EndFrame
**
** This does an implementation specific copy from the backbuffer to the
** front buffer.
*/
void SWimp_EndFrame (void)
{
	if (!X11_active)
		return;

	X_UpdateGrab ();

	if (x_scaler_stale)
		X_ResetScaler ();

	X_Present ();

	if (x_shared)
	{
		if (!XShmPutImage(x_disp, x_win, x_gc, x_image, 0, 0, 0, 0,
						  x_winw, x_winh, True))
			Sys_Error("VID_Update: XShmPutImage failed\n");
		// The image is about to be written again; the server has to have
		// finished reading it.
		oktodraw = false;
		while (!oktodraw)
			GetEvent();
	}
	else
		XPutImage(x_disp, x_win, x_gc, x_image, 0, 0, 0, 0, x_winw, x_winh);

	XSync(x_disp, False);
}

/*
** SWimp_SetMode
*/
rserr_t SWimp_SetMode( int *pwidth, int *pheight, int mode, qboolean fullscreen )
{
	rserr_t retval = rserr_ok;

	ri.Con_Printf (PRINT_ALL, "setting mode %d:", mode );

	if ( !ri.Vid_GetModeInfo( pwidth, pheight, mode ) )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return rserr_invalid_mode;
	}

	ri.Con_Printf( PRINT_ALL, " %d %d\n", *pwidth, *pheight);

	if ( !SWimp_InitGraphics( fullscreen ) ) {
		// failed to set a valid mode in windowed mode
		return rserr_invalid_mode;
	}

	R_GammaCorrectAndSetPalette( ( const unsigned char * ) d_8to24table );

	return retval;
}

/*
** SWimp_SetPalette
**
** System specific palette setting routine.  A NULL palette means
** to use the existing palette.  The palette is expected to be in
** a padded 4-byte xRGB format.
*/
void SWimp_SetPalette( const unsigned char *palette )
{
	int i;
	XColor colors[256];

	if (!X11_active)
		return;

    if ( !palette )
        palette = ( const unsigned char * ) sw_state.currentpalette;

	// The damage flash, the underwater tint and the cinematics are all
	// palette changes, so this table is rebuilt whenever they happen and
	// every frame goes through whichever is current.
	for(i=0;i<256;i++)
	{
		unsigned int	p = X_PixelFor (palette[i*4], palette[i*4+1], palette[i*4+2]);

		x_8to32[i] = p;
		x_8to16[i] = (unsigned short)p;
	}

	if (x_visinfo->class == PseudoColor && x_visinfo->depth == 8)
	{
		for (i=0 ; i<256 ; i++)
		{
			colors[i].pixel = i;
			colors[i].flags = DoRed|DoGreen|DoBlue;
			colors[i].red = palette[i*4] * 257;
			colors[i].green = palette[i*4+1] * 257;
			colors[i].blue = palette[i*4+2] * 257;
		}
		XStoreColors(x_disp, x_cmap, colors, 256);
	}
}

/*
** SWimp_Shutdown
**
** System specific graphics subsystem shutdown routine.
*/
void SWimp_Shutdown( void )
{
	if (!x_disp)
		return;

	if (x_grabbed)
	{
		XUngrabPointer (x_disp, CurrentTime);
		x_grabbed = false;
	}

	X_FreeImage ();
	free (x_pixels);
	x_pixels = NULL;
	free (x_outcols);
	x_outcols = NULL;

	if (x_win)
	{
		XDestroyWindow (x_disp, x_win);
		x_win = 0;
	}
	if (x_cmap)
	{
		XFreeColormap (x_disp, x_cmap);
		x_cmap = 0;
	}
	if (x_visinfo)
	{
		XFree (x_visinfo);
		x_visinfo = NULL;
	}

	if (x_own_screen)
		XAutoRepeatOn(x_disp);
	XCloseDisplay (x_disp);
	x_disp = NULL;
	x_fullscreen = false;
	x_scaler_stale = true;

	X11_active = false;
}

/*
** SWimp_AppActivate
*/
void SWimp_AppActivate( qboolean active )
{
}

//===============================================================================

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

	r = mprotect((char*)addr, length + startaddr - addr + psize, 7);

	if (r < 0)
    		Sys_Error("Protection change failed\n");

}

/*****************************************************************************/
/* KEYBOARD                                                                  */
/*****************************************************************************/

Key_Event_fp_t Key_Event_fp;

void KBD_Init(Key_Event_fp_t fp)
{
	Key_Event_fp = fp;
}

void KBD_Update(void)
{
// get events from x server
	if (x_disp)
	{
		while (XPending(x_disp))
			GetEvent();
		while (keyq_head != keyq_tail)
		{
			Key_Event_fp(keyq[keyq_tail].key, keyq[keyq_tail].down);
			keyq_tail = (keyq_tail + 1) & 63;
		}
	}
}

void KBD_Close(void)
{
	keyq_head = keyq_tail = 0;
}
