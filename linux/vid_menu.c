// vid_menu.c -- the Video menu, for the X11 software renderer.
//
// id's had a driver row (SVGAlib, X11, OpenGL) and two pages, one per kind of
// renderer. This build has one renderer, so it has one page: the mode list
// is every mode vid_so.c knows, in order of size, and fullscreen and the
// mouse are only offered where they mean something -- on a desktop, not in
// the container, where the window is the whole screen already and the
// browser has the pointer.

#include "../client/client.h"
#include "../client/qmenu.h"

extern cvar_t *vid_fullscreen;
extern cvar_t *vid_gamma;
extern cvar_t *scr_viewsize;

typedef struct vidmode_s
{
	const char *description;
	int         width, height;
	int         mode;
} vidmode_t;

extern vidmode_t vid_modes[];
int VID_NumModes (void);

static cvar_t *sw_mode;
static cvar_t *sw_stipplealpha;
static cvar_t *sw_colorlight;
static cvar_t *cl_shadowlights;
static cvar_t *_windowed_mouse;
static cvar_t *vid_resizescreen;

extern void M_ForceMenuOff( void );

/*
====================================================================

MENU INTERACTION

====================================================================
*/

#define MAX_MENU_MODES	32

static menuframework_s  s_software_menu;

static menulist_s		s_mode_list;
static menuslider_s		s_screensize_slider;
static menuslider_s		s_brightness_slider;
static menulist_s  		s_fs_box;
static menulist_s  		s_stipple_box;
static menulist_s		s_colorlight_box;
static menulist_s		s_shadowlights_box;
static menulist_s  		s_windowed_mouse;
static menuaction_s		s_apply_action;
static menuaction_s		s_defaults_action;

// The mode list, in the order shown, and the sw_mode each row sets. The last
// row is the custom size, when sw_mode -1 is in use.
static char				s_mode_names[MAX_MENU_MODES + 1][24];
static const char		*s_mode_items[MAX_MENU_MODES + 2];
static int				s_mode_for_item[MAX_MENU_MODES + 1];
static int				s_mode_count;

// sw_colorlight for each row of "coloured light"
static const float	s_colorlight_values[] = { 0, 0.5, 1 };

static void ColorLightCallback( void *s )
{
	menulist_s *list = ( menulist_s * ) s;

	Cvar_SetValue( "sw_colorlight", s_colorlight_values[list->curvalue] );
}

static void ShadowLightsCallback( void *s )
{
	Cvar_SetValue( "cl_shadowlights", ( ( menulist_s * ) s )->curvalue );
}

static void ScreenSizeCallback( void *s )
{
	menuslider_s *slider = ( menuslider_s * ) s;

	Cvar_SetValue( "viewsize", slider->curvalue * 10 );
}

static void BrightnessCallback( void *s )
{
	menuslider_s *slider = ( menuslider_s * ) s;
	float gamma = ( 0.8 - ( slider->curvalue/10.0 - 0.5 ) ) + 0.5;

	Cvar_SetValue( "vid_gamma", gamma );
}

static void ResetDefaults( void *unused )
{
	VID_MenuInit();
}

static void ApplyChanges( void *unused )
{
	float gamma;
	int mode;

	/*
	** invert sense so greater = brighter, and scale to a range of 0.5 to 1.3
	*/
	gamma = ( 0.8 - ( s_brightness_slider.curvalue/10.0 - 0.5 ) ) + 0.5;

	Cvar_SetValue( "vid_gamma", gamma );
	Cvar_SetValue( "sw_stipplealpha", s_stipple_box.curvalue );
	Cvar_SetValue( "sw_colorlight", s_colorlight_values[s_colorlight_box.curvalue] );
	Cvar_SetValue( "cl_shadowlights", s_shadowlights_box.curvalue );

	mode = s_mode_for_item[s_mode_list.curvalue];
	if ( mode != (int)sw_mode->value )
		Cvar_SetValue( "sw_mode", mode );

	if ( !vid_resizescreen->value )
	{
		Cvar_SetValue( "vid_fullscreen", s_fs_box.curvalue );
		Cvar_SetValue( "_windowed_mouse", s_windowed_mouse.curvalue);
	}

	M_ForceMenuOff();
}

/*
** VID_SortModes
**
** The rows of the mode list: every mode, smallest first, and the custom size
** at the end if that is what is in use.
*/
static void VID_SortModes( void )
{
	int i, j, n, t;
	int order[MAX_MENU_MODES];

	n = VID_NumModes();
	if ( n > MAX_MENU_MODES )
		n = MAX_MENU_MODES;

	for ( i = 0; i < n; i++ )
		order[i] = i;
	for ( i = 1; i < n; i++ )
	{
		for ( j = i; j > 0; j-- )
		{
			vidmode_t *a = &vid_modes[order[j-1]];
			vidmode_t *b = &vid_modes[order[j]];

			if ( a->width < b->width || ( a->width == b->width && a->height <= b->height ) )
				break;
			t = order[j-1]; order[j-1] = order[j]; order[j] = t;
		}
	}

	s_mode_count = 0;
	for ( i = 0; i < n; i++ )
	{
		vidmode_t *m = &vid_modes[order[i]];

		Com_sprintf( s_mode_names[s_mode_count], sizeof( s_mode_names[0] ),
					 "[%d %d]", m->width, m->height );
		s_mode_items[s_mode_count] = s_mode_names[s_mode_count];
		s_mode_for_item[s_mode_count] = order[i];
		s_mode_count++;
	}

	if ( (int)sw_mode->value == -1 )
	{
		Com_sprintf( s_mode_names[s_mode_count], sizeof( s_mode_names[0] ),
					 "[%d %d custom]", (int)Cvar_VariableValue( "vid_width" ) & ~7,
					 (int)Cvar_VariableValue( "vid_height" ) );
		s_mode_items[s_mode_count] = s_mode_names[s_mode_count];
		s_mode_for_item[s_mode_count] = -1;
		s_mode_count++;
	}

	s_mode_items[s_mode_count] = 0;
}

/*
** VID_MenuInit
*/
void VID_MenuInit( void )
{
	static const char *yesno_names[] =
	{
		"no",
		"yes",
		0
	};
	static const char *colorlight_names[] =
	{
		"off",
		"half",
		"full",
		0
	};
	int i, y;

	if ( !sw_colorlight )
		sw_colorlight = Cvar_Get( "sw_colorlight", "1", CVAR_ARCHIVE );
	if ( !cl_shadowlights )
		cl_shadowlights = Cvar_Get( "cl_shadowlights", "1", CVAR_ARCHIVE );
	if ( !sw_mode )
		sw_mode = Cvar_Get( "sw_mode", "0", 0 );
	if ( !sw_stipplealpha )
		sw_stipplealpha = Cvar_Get( "sw_stipplealpha", "0", CVAR_ARCHIVE );
	if ( !_windowed_mouse)
        _windowed_mouse = Cvar_Get( "_windowed_mouse", "0", CVAR_ARCHIVE );
	if ( !vid_resizescreen )
		vid_resizescreen = Cvar_Get( "vid_resizescreen", "0", CVAR_NOSET );
	if ( !scr_viewsize )
		scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);

	VID_SortModes();

	s_mode_list.curvalue = 0;
	for ( i = 0; i < s_mode_count; i++ )
		if ( s_mode_for_item[i] == (int)sw_mode->value )
			s_mode_list.curvalue = i;

	s_software_menu.x = viddef.width * 0.50;
	s_software_menu.nitems = 0;

	y = 0;

	s_mode_list.generic.type = MTYPE_SPINCONTROL;
	s_mode_list.generic.name = "video mode";
	s_mode_list.generic.x = 0;
	s_mode_list.generic.y = y;
	s_mode_list.itemnames = s_mode_items;
	y += 10;

	s_screensize_slider.generic.type	= MTYPE_SLIDER;
	s_screensize_slider.generic.x		= 0;
	s_screensize_slider.generic.y		= y;
	s_screensize_slider.generic.name	= "screen size";
	s_screensize_slider.minvalue = 3;
	s_screensize_slider.maxvalue = 12;
	s_screensize_slider.curvalue = scr_viewsize->value/10;
	s_screensize_slider.generic.callback = ScreenSizeCallback;
	y += 10;

	s_brightness_slider.generic.type	= MTYPE_SLIDER;
	s_brightness_slider.generic.x	= 0;
	s_brightness_slider.generic.y	= y;
	s_brightness_slider.generic.name	= "brightness";
	s_brightness_slider.generic.callback = BrightnessCallback;
	s_brightness_slider.minvalue = 5;
	s_brightness_slider.maxvalue = 13;
	s_brightness_slider.curvalue = ( 1.3 - vid_gamma->value + 0.5 ) * 10;
	y += 10;

	s_stipple_box.generic.type = MTYPE_SPINCONTROL;
	s_stipple_box.generic.x	= 0;
	s_stipple_box.generic.y	= y;
	s_stipple_box.generic.name	= "stipple alpha";
	s_stipple_box.curvalue = sw_stipplealpha->value;
	s_stipple_box.itemnames = yesno_names;
	y += 10;

	// the maps' coloured light: none, as in 1997; half; all of it
	s_colorlight_box.generic.type = MTYPE_SPINCONTROL;
	s_colorlight_box.generic.x	= 0;
	s_colorlight_box.generic.y	= y;
	s_colorlight_box.generic.name	= "coloured light";
	s_colorlight_box.generic.callback = ColorLightCallback;
	s_colorlight_box.itemnames = colorlight_names;
	s_colorlight_box.curvalue = sw_colorlight->value <= 0 ? 0
		: sw_colorlight->value < 1 ? 1 : 2;
	y += 10;

	// the 2023 re-release's real-time lights, in its remade maps
	s_shadowlights_box.generic.type = MTYPE_SPINCONTROL;
	s_shadowlights_box.generic.x	= 0;
	s_shadowlights_box.generic.y	= y;
	s_shadowlights_box.generic.name	= "lamp lights";
	s_shadowlights_box.generic.callback = ShadowLightsCallback;
	s_shadowlights_box.itemnames = yesno_names;
	s_shadowlights_box.curvalue = cl_shadowlights->value != 0;
	y += 10;

	s_fs_box.generic.type = MTYPE_SPINCONTROL;
	s_fs_box.generic.x	= 0;
	s_fs_box.generic.y	= y;
	s_fs_box.generic.name	= "fullscreen";
	s_fs_box.itemnames = yesno_names;
	s_fs_box.curvalue = vid_fullscreen->value;

	s_windowed_mouse.generic.type = MTYPE_SPINCONTROL;
	s_windowed_mouse.generic.x  = 0;
	s_windowed_mouse.generic.y  = y + 10;
	s_windowed_mouse.generic.name   = "hold mouse";
	s_windowed_mouse.curvalue = _windowed_mouse->value;
	s_windowed_mouse.itemnames = yesno_names;

	if ( !vid_resizescreen->value )
		y += 20;

	s_defaults_action.generic.type = MTYPE_ACTION;
	s_defaults_action.generic.name = "reset to default";
	s_defaults_action.generic.x    = 0;
	s_defaults_action.generic.y    = y + 10;
	s_defaults_action.generic.callback = ResetDefaults;

	s_apply_action.generic.type = MTYPE_ACTION;
	s_apply_action.generic.name = "apply";
	s_apply_action.generic.x    = 0;
	s_apply_action.generic.y    = y + 20;
	s_apply_action.generic.callback = ApplyChanges;

	Menu_AddItem( &s_software_menu, ( void * ) &s_mode_list );
	Menu_AddItem( &s_software_menu, ( void * ) &s_screensize_slider );
	Menu_AddItem( &s_software_menu, ( void * ) &s_brightness_slider );
	Menu_AddItem( &s_software_menu, ( void * ) &s_stipple_box );
	Menu_AddItem( &s_software_menu, ( void * ) &s_colorlight_box );
	Menu_AddItem( &s_software_menu, ( void * ) &s_shadowlights_box );
	if ( !vid_resizescreen->value )
	{
		Menu_AddItem( &s_software_menu, ( void * ) &s_fs_box );
		Menu_AddItem( &s_software_menu, ( void * ) &s_windowed_mouse );
	}
	Menu_AddItem( &s_software_menu, ( void * ) &s_defaults_action );
	Menu_AddItem( &s_software_menu, ( void * ) &s_apply_action );

	Menu_Center( &s_software_menu );
	s_software_menu.x -= 8;
}

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	int w, h;

	/*
	** draw the banner
	*/
	re.DrawGetPicSize( &w, &h, "m_banner_video" );
	re.DrawPic( viddef.width / 2 - w / 2, viddef.height /2 - 110, "m_banner_video" );

	/*
	** move cursor to a reasonable starting position
	*/
	Menu_AdjustCursor( &s_software_menu, 1 );

	/*
	** draw the menu
	*/
	Menu_Draw( &s_software_menu );
}

/*
================
VID_MenuKey
================
*/
const char *VID_MenuKey( int key )
{
	extern void M_PopMenu( void );

	menuframework_s *m = &s_software_menu;
	static const char *sound = "misc/menu1.wav";

	switch ( key )
	{
	case K_ESCAPE:
	case K_BACKSPACE:
		M_PopMenu();
		return NULL;
	case K_UPARROW:
		m->cursor--;
		Menu_AdjustCursor( m, -1 );
		break;
	case K_DOWNARROW:
		m->cursor++;
		Menu_AdjustCursor( m, 1 );
		break;
	case K_LEFTARROW:
		Menu_SlideItem( m, -1 );
		break;
	case K_RIGHTARROW:
		Menu_SlideItem( m, 1 );
		break;
	case K_ENTER:
		Menu_SelectItem( m );
		break;
	}

	return sound;
}
