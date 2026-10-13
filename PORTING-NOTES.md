# Porting notes

Every change made to id's 2001 sources, and why. The original code is in
git history (`372afde`); the files below are relative to the top of the tree.

The approach is the one [the Quake port](https://github.com/Krippler/Quake)
took: build what still makes sense as portable C, repair what a 64-bit
compiler breaks, replace what the code talked to that no longer exists, and
leave id's code alone otherwise. The Windows, IRIX, Solaris, Rhapsody and
OpenGL trees are untouched and unbuilt.

## The build

`linux/Makefile` is new. Zoid's `Makefile.i386` built seven targets against
gcc 2.7, an i386 assembler, SVGAlib, 3dfx Glide and Mesa 2.6, from a tree it
expected at `/grog/NewWork/Quake2/code`. The new one builds six, as C:

- `quake2`, the engine, linked `-rdynamic` so a crash backtrace has names in
  it;
- `ref_softx.so`, the software renderer drawing to X11;
- `baseq2/gamex86_64.so` and `ctf/gamex86_64.so`, the two game libraries
  whose source is in the release;
- `xatrix/gamex86_64.so` and `rogue/gamex86_64.so`, the mission packs, from
  Yamagi Quake II's GPLv2 code (see [The mission packs](#the-mission-packs)).

The assembler is 32-bit only, and `q_shared.h` already turns it off wherever
`__i386__` is not defined (`id386` 0), so nothing had to be done to leave it
out; `-DC_ONLY` says so anyway.

`-fno-strict-aliasing` because the renderer reads floats through integer
pointers; `-fwrapv` because the fixed-point maths overflows on purpose;
`-fcommon` because the headers define variables rather than declaring them,
and gcc 10 stopped merging those.

The libraries are linked `-Wl,-Bsymbolic`. Each carries its own copy of
`q_shared.c`, and the renderer its own `Hunk_Begin`; with the engine
exporting every symbol (`-rdynamic`), a library's calls to those resolved to
the engine's copies instead. AddressSanitizer reported it as an ODR violation
on `vec3_origin`. `-Bsymbolic` binds each library to its own definitions, as
it was when the engine exported nothing.

The game library is `gamex86_64.so`, not id's `gamei386.so`, so that a mod
built for Yamagi Quake II, whose game API is id's, drops in.

## 64-bit repairs

- **`ref_soft/r_poly.c`**: `R_ClipAndDrawPoly` stepped through its vertices
  with `pv += sizeof (vec5_t) / sizeof (pv)`. `sizeof (pv)` is the size of the
  pointer, four bytes in 1997, so a step of five floats, one vertex. Eight
  here: a step of two. Every translucent surface -- water, glass, force fields
  -- was drawn from the wrong vertices, came out twisted, and its left and
  right edges disagreed on how many rows it covered, so the span list's end
  marker landed short and `R_PolygonDrawSpans` walked into the stack. It
  crashed on the first window in the demo's first room. Now `sizeof (*pv)`.
- **`qcommon/md4.c`**: `UINT4`, MD4's 32-bit word, was `unsigned long` -- 64
  bits on LP64. `Com_BlockChecksum` is the map checksum a client and server
  compare, and it came out different from every other Quake II's.
- **`game/g_local.h`, `ctf/g_local.h`**: `FOFS` and friends, the field offsets
  the savegame and spawn code are driven by, took the address of a member of a
  struct at address zero and cast it to an `int`. Now `offsetof`.
- **`server/sv_world.c`**: `STRUCT_FROM_LINK` the same way. Now `offsetof`.
- **`ref_soft/r_surf.c`**: the surface cache block size the same way. Now
  `offsetof`.
- **`server/sv_ents.c`**: `SV_FatPVS` merged PVS rows as `long`s over a count
  of 32-bit words, reading and writing twice as far as it meant to.
- **`client/cl_scrn.c`**: `entitycmpfnc` sorted by subtracting pointers cast
  to `int`; the sign of a truncated 64-bit difference means nothing. Compared
  instead.
- **`ref_soft/r_edge.c`**, **`client/snd_mem.c`**: pointer-to-`int` casts in a
  debug colouring and a debug print.
- **`linux/q_shlinux.c`**: the hunk's header was `sizeof(int)`, leaving
  everything in it 4-byte aligned; it holds pointers and floats that x86-64
  code may load with aligned SSE instructions. 32 bytes now.

## Undefined behaviour

- **`ref_soft/r_model.c`**: `Mod_LoadTexinfo` copied `vecs[0][j]` for `j` up to
  8, walking the first row of `float vecs[2][4]` into the second. gcc
  diagnoses it ("iteration 4 invokes undefined behavior") and may cut the loop
  short, taking every surface's second texture axis with it. The Quake port
  found the same loop in Quake's `model.c`.
- **`client/cl_fx.c`**: `avelocities[0][i]` for `i` up to
  `NUMVERTEXNORMALS*3`, the same shape, twice.
- **`ref_soft/r_scan.c`, `r_rast.c`, `r_main.c`, `r_local.h`**: `D_WarpScreen`,
  the underwater warp, reads the turbulence tables at the frame's width plus
  an offset of up to 127. The tables were 1280 entries, already short at
  1600 wide; they are `MAXWIDTH + CYCLE` now.
- **`game/g_items.c`, `ctf/g_items.c`, `ctf/p_weapon.c`**: variables declared
  `static` in the file and `extern` in the header, a hard error now.
- **`ctf/g_ctf.c`**: `CTFDrop_Flag` returned `qboolean` and was called through
  a pointer to a function returning `void`.
- **`game/m_boss31.c`**: an `extern` with no type.
- **`game/q_shared.h`, `ctf/q_shared.h`**: `<ctype.h>`, `<stddef.h>` and
  `<stdint.h>`, for `tolower`, `offsetof` and `intptr_t`. `linux/net_udp.c`
  gets `<arpa/inet.h>` for `inet_addr`, `linux/rw_x11.c` `<sys/mman.h>`.

## Sound

- **`client/snd_mix.c`**: the C version of `S_PaintChannelFrom8` indexed its
  volume table with `leftvol >> 11`, which is row 0 -- silence -- for every
  volume from 0 to 255. id's x86 builds used the assembler version, so
  nobody heard it; on any other machine every sound loaded as 8 bits played at
  nothing, and `s_loadas8bit` is on by default. `>> 3`, as the table is laid
  out.
- **`linux/snd_stream.c`** (new): a backend with no sound card. `snd_linux.c`
  mmaps `/dev/dsp`, which no current kernel provides. This keeps its own
  monotonic clock and writes the mixed 16-bit stereo at 22050 Hz to a FIFO
  (`$QUAKE2_AUDIO_FIFO`), which `audiostream` serves to the browser. From the
  Quake port, where the reasons for each detail are written up.
- **`linux/snd_alsa.c`** (new): the desktop build's backend, ALSA's default
  device, which is PipeWire or PulseAudio where either runs.
- **`client/snd_dma.c`**, **`client/snd_loc.h`**: with `SND_HAS_GETSAMPLES`,
  `GetSoundtime` asks the backend for the playback position as a running count
  instead of reconstructing it by counting ring wraps -- which id's comment
  admits miscounts after a long frame, and a level load is one. The Quake port
  measured what that cost: silence for the rest of the run.

## Music

- **`linux/cd_stream.c`** (new): the soundtrack was CD audio, played by the
  drive. This looks the same track numbers up as files -- `track02.ogg` and
  so on, any case, in Ogg, Opus, FLAC, MP3 or WAV through libsndfile -- and
  mixes them into the engine's output (`client/snd_mix.c` calls
  `CDAudio_MixPaintBuffer`). It looks in the game's `music` directory, then
  `$QUAKE2_MUSICDIR`, then `baseq2/music`, then a `music` directory beside
  `baseq2`, and again whenever the game directory changes. The `cd` command
  and the menu's CD music switch (`cd_nocd`) work as before; `cd_volume`
  scales it.

## The system layer

**`linux/sys_linux.c`**:

- stdout is line-buffered, and a crash handler prints the signal, the address
  and a backtrace with `write()` and `backtrace_symbols_fd`, then re-raises so
  the exit status is still the signal's.
- SIGTERM, SIGINT and SIGHUP raise a flag the frame loop turns into
  `Com_Quit`, which writes `config.cfg`. There was no handler, so `docker
  stop` lost every setting changed since the last menu quit.
- The frame loop spun until the clock moved a millisecond, pinning a core.
  It sleeps instead.
- `Sys_GetGameAPI` loads `gamex86_64.so` from the search path first, so a mod's
  own library wins, then from beside the engine (`Sys_ExeDir`). It also used
  the current directory as a prefix for every search path, which made an
  absolute `basedir` fail.
- The CD check, compiled out in the release, is a stub; the setuid juggling
  for SVGAlib is gone.

**`linux/q_shlinux.c`**: `Sys_Milliseconds` reads `CLOCK_MONOTONIC`, not the
wall clock, which NTP can step backwards. `strlwr` returned nothing.

## Video

**`linux/rw_x11.c`** is largely rewritten.

- It drew into the XImage and expanded each frame in place to 16 bits, the
  only deeper visual it knew. At depth 24 the picture was noise. The renderer
  now draws into a buffer of its own and each frame is translated through the
  palette into an image at whatever depth the server has: 8 (PseudoColor,
  with the engine installing its own colormap), 15, 16 or 24.
- `XSynchronize(True)`, "for debugging only", made every Xlib call a round
  trip.
- With `vid_resizescreen 1` (the container) the window is the whole screen,
  and a mode change resizes the screen through RANDR 1.2, creating the mode
  first; Xvfb starts knowing one. On a desktop the frame is scaled into the
  window, as large as fits, whatever size it is made, and fullscreen is
  `_NET_WM_STATE_FULLSCREEN`.
- Signal handlers that called Sys_Error from inside the handler are gone.
- Key repeat was turned off for the whole X server; on a desktop it uses
  detectable autorepeat instead. Keys are looked up unshifted, so a key's
  press and release agree.
- Mouse motion was assigned rather than added, losing all but the last report
  of a frame; the wheel was ignored; movement in a menu was saved up and spent
  on the way back to the game. In the container a motion report exactly on the
  centre is the page re-basing the pointer and is not a movement. On a desktop
  the pointer is held only while playing in the focused window.
- `WM_DELETE_WINDOW` quits through the engine. Shared memory is `IPC_PRIVATE`
  and falls back to the socket when the server cannot attach it. `$DISPLAY` is
  read, not written to.

**`linux/vid_so.c`**: the renderer is found beside the engine rather than
through `/etc/quake2.conf`, which only root could write, and `softx` is the
only renderer -- a config naming `gl` or `soft` is put back to it. The mode
table keeps id's ten modes at their numbers and adds widescreen ones up to
1920x1200; `sw_mode -1` is `vid_width` by `vid_height`.

**`linux/vid_menu.c`**: one page, for the one renderer: every mode in order of
size, and fullscreen and the mouse grab where they mean something.

**`ref_soft/r_local.h`**: `MAXWIDTH` 1600 to 1920.

**`client/cl_view.c`**: Hor+. The fov is the horizontal angle of a 4:3
picture; wider than that, the vertical angle is kept and the sides are added,
rather than the top and bottom cropped.

## The mission packs

`xatrix/` (The Reckoning) and `rogue/` (Ground Zero) are not id's code as
released. id published their game source in 1998 (`xatrixsrc320`,
`roguesrc320`) under its Limited Program Source Code License, which does not
allow distributing modified copies and is not the GPL. Yamagi Quake II's
versions of both are distributed under GPLv2, are already 64-bit clean, and
keep id's game API (version 3), so they are carried here unmodified:
`src/`, `LICENSE`, `README.md` and `CHANGELOG` as upstream has them, and
`UPSTREAM` naming the repository and commit.

Checked against this engine before they went in:

- `game_import_t` and `game_export_t` differ from `game/game.h` only in
  `const` qualifiers, which change nothing in the calling convention.
- Every struct the engine and a game library share has the same size and
  member offsets, except `cvar_t`: Yamagi appended `default_string` to the
  end, which the game code never reads, so the engine's shorter struct is
  read correctly.
- `GetGameAPI` is the only symbol they export (`-fvisibility=hidden`).

`linux/Makefile` builds them with Yamagi's own flags rather than the 1997
code's, plus the two names their savegames carry, `YQ2OSTYPE` "Linux" and
`YQ2ARCH` from `uname -m` -- the same as Yamagi's own builds, so a save
moves between the two. The smoke test loads its map under each and saves
and loads a game.

## Input

- **`linux/in_pad.c`** (new): game controllers, from the browser over a
  local TCP port in the container (`$QUAKE2_PAD_PORT`) or from SDL2, loaded
  with `dlopen`, on a desktop. Buttons are keys `PAD_A` to `PAD_GUIDE`
  (`client/keys.c` names them, on `K_AUX1` onwards), bound in Customize
  Controls; the sticks move and look. From the Quake port.
- **`client/keys.c`, `client/menu.c`, `client/qmenu.c`**: Backspace is the
  menu key, because in the browser Escape belongs to the page. From the game
  it does what Escape does; in a menu it goes back unless the cursor is on a
  text field; it cannot be bound (id's `default.cfg` binds it to `invdrop`,
  which is dropped quietly). On the key bindings screen Del clears a binding.
- **`linux/rw_linux.h`**: the renderer is given `cls.key_dest`, to know when
  the game has the keys.
