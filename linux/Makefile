# Quake II (3.21 GPL release) for a current 64-bit Linux.
#
# Zoid's Makefile.i386 built seven targets against gcc 2.7, an i386
# assembler, SVGAlib, 3dfx Glide and Mesa 2.6, from a tree mounted at
# /grog/NewWork/Quake2/code. This builds the ones that still make sense --
# the engine, the X11 software renderer and the game libraries: the two
# whose source is in id's release, and the mission packs -- as portable C, because the assembler is 32-bit only and
# q_shared.h already turns it off (id386 is 0 wherever __i386__ is not
# defined, which is here).
#
#   make              # -> build/, for the container
#   make SOUND=alsa   # for a Linux desktop: sound through ALSA, which is
#                     # PipeWire or PulseAudio where either is running
#   make SOUND=oss    # the 1997 /dev/dsp backend instead of the streaming one
#   make install      # see INSTALLING below
#   make clean
#
# What comes out:
#
#   build/quake2                     the engine
#   build/ref_softx.so               the software renderer, drawing to X11
#   build/baseq2/gamex86_64.so       the single-player game
#   build/ctf/gamex86_64.so          Threewave CTF
#   build/xatrix/gamex86_64.so       The Reckoning, the first mission pack
#   build/rogue/gamex86_64.so        Ground Zero, the second
#
# The engine finds the renderer and the game libraries beside itself, so the
# directory can be moved anywhere as a whole. A game directory in the data
# that brings its own gamex86_64.so -- a mod built for this engine or for
# Yamagi Quake II, whose game API is the same -- is used in preference.
#
# See ../PORTING-NOTES.md for what had to change in the sources themselves.

B = build

CC      ?= gcc
STRIP   ?= strip

# -fno-strict-aliasing is not optional: the renderer reads floats through
# integer pointers to get at their bits (r_main.c, r_polyse.c, q_shared.c's
# Q_fabs), which gcc 3 and later may assume never happens. -fwrapv likewise:
# the fixed-point maths overflows on purpose. -fcommon because the 1997 headers
# define variables, not just declare them, and gcc 10 stopped merging those.
BASE_CFLAGS = -Dstricmp=strcasecmp -D_GNU_SOURCE -DC_ONLY \
              -fno-strict-aliasing -fwrapv -fcommon -fPIC

# Warnings the 1997 sources trip over constantly and that say nothing about
# whether the port works. Everything else is left on.
QUIET_WARNINGS = -Wno-unused-result -Wno-unused-but-set-variable \
                 -Wno-unused-variable -Wno-unused-function \
                 -Wno-stringop-truncation -Wno-format-truncation \
                 -Wno-format-overflow -Wno-misleading-indentation \
                 -Wno-char-subscripts -Wno-dangling-else \
                 -Wno-pointer-sign -Wno-stringop-overflow \
                 -Wno-array-bounds -Wno-maybe-uninitialized \
                 -Wno-implicit-fallthrough

OPT_CFLAGS = -O2 -ffast-math -funroll-loops -fomit-frame-pointer
DEBUG_CFLAGS = -O0 -g3 -fno-omit-frame-pointer

ifeq ($(DEBUG),1)
CFLAGS ?= $(DEBUG_CFLAGS)
else
CFLAGS ?= $(OPT_CFLAGS)
endif

ALL_CFLAGS = $(BASE_CFLAGS) $(QUIET_WARNINGS) $(CFLAGS) $(MUSIC_CFLAGS) \
             $(SND_CFLAGS) $(EXTRA_CFLAGS)

# Which sound backend to link. "stream" writes the mixed PCM to a pipe for
# audiostream to pick up, which is how sound reaches the browser; "alsa" plays
# it on the desktop the engine is running on; "oss" is the 1997 /dev/dsp code,
# kept for a machine that still has one.
SOUND ?= stream

ifeq ($(SOUND),oss)
SND_SRC = snd_linux.c
else
ifeq ($(SOUND),alsa)
SND_SRC = snd_alsa.c
SND_CFLAGS = -DSND_HAS_GETSAMPLES $(shell pkg-config --cflags alsa)
SND_LDFLAGS = $(shell pkg-config --libs alsa)
else
SND_SRC = snd_stream.c
# snd_stream.c keeps the playback position as a running count, so GetSoundtime
# can ask for it instead of reconstructing it from a ring position.
SND_CFLAGS = -DSND_HAS_GETSAMPLES
endif
endif

# Where the music comes from. "sndfile" decodes Ogg Vorbis, FLAC, WAV, Opus
# and, where the installed libsndfile has it, MP3, from a music directory --
# because Quake II's music was CD audio and the container has no drive. "cd"
# is the 1997 /dev/cdrom code; "none" leaves music out.
MUSIC ?= sndfile

ifeq ($(MUSIC),cd)
CD_SRC = cd_linux.c
else
CD_SRC = cd_stream.c
ifneq ($(MUSIC),none)
MUSIC_CFLAGS = -DQUAKE_MUSIC_SNDFILE $(shell pkg-config --cflags sndfile)
MUSIC_LDFLAGS = $(shell pkg-config --libs sndfile)
endif
endif

# -rdynamic puts the engine's own symbols in the dynamic symbol table, which is
# where backtrace_symbols_fd looks: a crash backtrace with function names in
# it rather than a column of addresses. -ldl for the renderer and game
# libraries, which are dlopened.
#
# That table is also where a dlopened library's references are resolved
# first, and the renderer and the game libraries each carry their own copy of
# q_shared.c and friends -- the renderer even has its own Hunk_Begin. So each
# library is linked -Bsymbolic, binding it to its own definitions, as it was
# when the engine exported nothing.
ENGINE_LDFLAGS = -rdynamic -lm -ldl $(MUSIC_LDFLAGS) $(SND_LDFLAGS) $(EXTRA_LDFLAGS)

#############################################################################
# The engine: client, server and common code.
#############################################################################
CLIENT_SRCS = \
	client/cl_cin.c client/cl_ents.c client/cl_fx.c client/cl_input.c \
	client/cl_inv.c client/cl_main.c client/cl_parse.c client/cl_pred.c \
	client/cl_tent.c client/cl_scrn.c client/cl_view.c client/cl_newfx.c \
	client/cl_loc.c \
	client/console.c client/keys.c client/menu.c client/snd_dma.c \
	client/snd_mem.c client/snd_mix.c client/qmenu.c \
	game/m_flash.c \
	qcommon/cmd.c qcommon/cmodel.c qcommon/common.c qcommon/crc.c \
	qcommon/cvar.c qcommon/files.c qcommon/md4.c qcommon/net_chan.c \
	qcommon/pmove.c qcommon/usable.c \
	server/sv_ccmds.c server/sv_ents.c server/sv_game.c server/sv_init.c \
	server/sv_main.c server/sv_rrdemo.c server/sv_send.c server/sv_user.c server/sv_world.c \
	linux/$(CD_SRC) linux/q_shlinux.c linux/vid_menu.c linux/vid_so.c \
	linux/$(SND_SRC) linux/sys_linux.c linux/glob.c linux/net_udp.c \
	linux/in_pad.c \
	game/q_shared.c

#############################################################################
# The software renderer, for X11.
#############################################################################
REF_SOFT_SRCS = \
	ref_soft/r_aclip.c ref_soft/r_alias.c ref_soft/r_bsp.c ref_soft/r_draw.c \
	ref_soft/r_edge.c ref_soft/r_fog.c ref_soft/r_image.c ref_soft/r_png.c ref_soft/r_light.c ref_soft/r_main.c ref_soft/r_md5.c \
	ref_soft/r_misc.c ref_soft/r_model.c ref_soft/r_part.c ref_soft/r_poly.c \
	ref_soft/r_polyse.c ref_soft/r_rast.c ref_soft/r_scan.c \
	ref_soft/r_sprite.c ref_soft/r_surf.c qcommon/usable.c \
	game/q_shared.c linux/q_shlinux.c linux/glob.c linux/rw_x11.c

# -lXrandr lets the video menu resize the X screen as well as the window; see
# RW_SetScreenSize in rw_x11.c.
REF_SOFT_LDFLAGS = -lX11 -lXext -lXrandr -lz -lm

#############################################################################
# The game libraries.
#############################################################################
# Everything is built from the top of the tree, which is where the sources'
# own ../client/ and ../qcommon/ includes are relative to.
TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
VPATH = $(TOP)

# m_flash.c is the muzzle flash table, which the engine builds in as well.
GAME_SRCS = $(patsubst $(TOP)/%,%,$(wildcard $(TOP)/game/g_*.c $(TOP)/game/m_*.c \
              $(TOP)/game/p_*.c)) game/q_shared.c
CTF_SRCS  = $(patsubst $(TOP)/%,%,$(wildcard $(TOP)/ctf/g_*.c $(TOP)/ctf/m_*.c \
              $(TOP)/ctf/p_*.c)) ctf/q_shared.c

# The mission packs are Yamagi Quake II's GPLv2 versions, kept as they come
# from upstream but for two fixes the 2023 re-release's maps need (listed in
# xatrix/UPSTREAM and rogue/UPSTREAM): id's own 1998 source
# for them is under a licence that does not allow modified copies. Built
# with Yamagi's own flags, not the 1997 code's, since they are already
# 64-bit clean. YQ2OSTYPE and YQ2ARCH go into their savegames, and match
# Yamagi's own builds so saves move between the two.
MP_CFLAGS = -O2 -fno-strict-aliasing -fwrapv -fvisibility=hidden -fPIC \
            -Wno-format-truncation -Wno-format-overflow \
            -DYQ2OSTYPE=\"Linux\" -DYQ2ARCH=\"$(shell uname -m)\" \
            $(EXTRA_CFLAGS)
XATRIX_SRCS = $(patsubst $(TOP)/%,%,$(shell find $(TOP)/xatrix/src -name '*.c'))
ROGUE_SRCS  = $(patsubst $(TOP)/%,%,$(shell find $(TOP)/rogue/src -name '*.c'))

# Each library gets its own object directory: q_shared.c and friends are built
# into all of them, and the renderer's copy has to be built without the
# engine's sound and music flags.
ENGINE_OBJS = $(patsubst %.c,$(B)/obj/engine/%.o,$(CLIENT_SRCS))
REF_OBJS    = $(patsubst %.c,$(B)/obj/ref_soft/%.o,$(REF_SOFT_SRCS))
GAME_OBJS   = $(patsubst %.c,$(B)/obj/baseq2/%.o,$(GAME_SRCS))
CTF_OBJS    = $(patsubst %.c,$(B)/obj/ctf/%.o,$(CTF_SRCS))
XATRIX_OBJS = $(patsubst %.c,$(B)/obj/xatrix/%.o,$(XATRIX_SRCS))
ROGUE_OBJS  = $(patsubst %.c,$(B)/obj/rogue/%.o,$(ROGUE_SRCS))

TARGETS = $(B)/quake2 $(B)/ref_softx.so $(B)/baseq2/gamex86_64.so \
          $(B)/ctf/gamex86_64.so $(B)/xatrix/gamex86_64.so \
          $(B)/rogue/gamex86_64.so

all: $(TARGETS)

$(B)/quake2: $(ENGINE_OBJS)
	$(CC) $(ALL_CFLAGS) -o $@ $^ $(ENGINE_LDFLAGS)

$(B)/ref_softx.so: $(REF_OBJS)
	$(CC) $(ALL_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $^ $(REF_SOFT_LDFLAGS)

$(B)/baseq2/gamex86_64.so: $(GAME_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $^ -lm -ldl

$(B)/ctf/gamex86_64.so: $(CTF_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $^ -lm -ldl

$(B)/obj/engine/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -MMD -MP -o $@ -c $<

$(B)/obj/ref_soft/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -MMD -MP -o $@ -c $<

$(B)/xatrix/gamex86_64.so: $(XATRIX_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(MP_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $^ -lm

$(B)/rogue/gamex86_64.so: $(ROGUE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(MP_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $^ -lm

$(B)/obj/xatrix/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(MP_CFLAGS) -MMD -MP -o $@ -c $<

$(B)/obj/rogue/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(MP_CFLAGS) -MMD -MP -o $@ -c $<

$(B)/obj/baseq2/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -MMD -MP -o $@ -c $<

$(B)/obj/ctf/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -MMD -MP -o $@ -c $<

# Zoid's Makefile listed no header dependencies at all, so editing qcommon.h
# rebuilt nothing and left a binary half-built against the old one.
-include $(ENGINE_OBJS:.o=.d) $(REF_OBJS:.o=.d) $(GAME_OBJS:.o=.d) $(CTF_OBJS:.o=.d) \
         $(XATRIX_OBJS:.o=.d) $(ROGUE_OBJS:.o=.d)

strip: $(TARGETS)
	$(STRIP) $(TARGETS)

# INSTALLING
#
# For a desktop, built with SOUND=alsa:
#
#   make SOUND=alsa
#   sudo make SOUND=alsa install              # into /usr/local
#   make SOUND=alsa install PREFIX=~/.local   # or for one user
#
# The same layout as the release tarball's install.sh: the launcher in bin,
# the engine and its libraries in lib/quake2, and a menu entry and an icon.
# DESTDIR is for packagers. A build for the container has no sound on a
# desktop, so it is not installed.
PREFIX ?= /usr/local
DESKTOP = $(TOP)/desktop
LIBDIR = $(DESTDIR)$(PREFIX)/lib/quake2

install: $(TARGETS)
ifneq ($(SOUND),alsa)
	$(error make install is for the desktop build: make SOUND=alsa install)
endif
	install -d $(DESTDIR)$(PREFIX)/bin $(LIBDIR)/baseq2 $(LIBDIR)/ctf \
		$(LIBDIR)/xatrix $(LIBDIR)/rogue \
		$(DESTDIR)$(PREFIX)/share/applications \
		$(DESTDIR)$(PREFIX)/share/icons/hicolor/48x48/apps
	install -m 755 $(B)/quake2 $(LIBDIR)/quake2
	install -m 755 $(B)/ref_softx.so $(LIBDIR)/ref_softx.so
	install -m 755 $(B)/baseq2/gamex86_64.so $(LIBDIR)/baseq2/gamex86_64.so
	install -m 755 $(B)/ctf/gamex86_64.so $(LIBDIR)/ctf/gamex86_64.so
	install -m 755 $(B)/xatrix/gamex86_64.so $(LIBDIR)/xatrix/gamex86_64.so
	install -m 755 $(B)/rogue/gamex86_64.so $(LIBDIR)/rogue/gamex86_64.so
	install -m 755 $(DESKTOP)/quake2 $(DESTDIR)$(PREFIX)/bin/quake2
	install -m 644 $(DESKTOP)/quake2.png \
		$(DESTDIR)$(PREFIX)/share/icons/hicolor/48x48/apps/quake2.png
	sed 's|^Exec=quake2|Exec="$(PREFIX)/bin/quake2"|' $(DESKTOP)/quake2.desktop \
		> $(DESTDIR)$(PREFIX)/share/applications/quake2.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/quake2 \
		$(LIBDIR)/quake2 $(LIBDIR)/ref_softx.so \
		$(LIBDIR)/baseq2/gamex86_64.so $(LIBDIR)/ctf/gamex86_64.so \
		$(LIBDIR)/xatrix/gamex86_64.so $(LIBDIR)/rogue/gamex86_64.so \
		$(DESTDIR)$(PREFIX)/share/applications/quake2.desktop \
		$(DESTDIR)$(PREFIX)/share/icons/hicolor/48x48/apps/quake2.png
	-rmdir $(LIBDIR)/baseq2 $(LIBDIR)/ctf $(LIBDIR)/xatrix $(LIBDIR)/rogue $(LIBDIR)

clean:
	rm -rf $(B)

.PHONY: all clean strip install uninstall
