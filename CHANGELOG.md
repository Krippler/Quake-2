# Changelog

All notable changes are here. The format follows Keep a Changelog, and the
top section's heading is what the release workflow reads: `## [X.Y.Z] — DATE`
on the default branch publishes that version, `## [Unreleased]` publishes only
`edge`.

## [1.0.0] — 2026-10-13

The 2001 GPL release of Quake II, building and running on a current 64-bit
Linux, playable in a browser from a container, and installable on a Linux
desktop. [PORTING-NOTES.md](PORTING-NOTES.md) has every change to id's code.

### Fixed

- **Translucent surfaces crashed the renderer.** A loop stepped through their
  vertices by the size of a pointer rather than a float, so water, glass and
  force fields were built from the wrong vertices and their span lists ran
  into the stack.
- **Every 8-bit sound played silent** in the C mixer, which id's x86 builds
  never used.
- **Map checksums** came out different from every other Quake II: MD4's
  32-bit word was an `unsigned long`.
- Field offsets, area links, the surface cache size and the PVS merge took
  pointers and widths as 32 bits.
- Two loops walked off the end of an array row, which gcc may delete; the
  underwater warp read past its tables at 1600 wide.
- The X11 driver drew noise at depth 24, ran synchronously, lost mouse
  motion, ignored the wheel, turned key repeat off for the whole X server,
  and called `Sys_Error` from a signal handler.
- `docker stop` and closing the window lost every setting changed since the
  last menu quit: SIGTERM and `WM_DELETE_WINDOW` now quit through the engine,
  which writes `config.cfg`.
- The frame loop spun a core at 100%.

### Added

- A container that plays in a browser: noVNC with the mouse captured, the
  sound over the same port, a game controller from the page, and a writable
  game directory assembled from read-only game data.
- Music from track files (`baseq2/music/track02.ogg` ...), mixed into the
  game's own sound.
- Video modes up to 1920x1200, widescreen included, with Hor+ field of view;
  the container's X screen follows the mode.
- Backspace is the menu key, since `Esc` belongs to the browser.
- WASD and mouse look on a fresh state volume (`QUAKE2_MODERN_CONTROLS=0`
  for id's).
- Game controllers: buttons bound as keys (`PAD_A` ...), sticks to move and
  look; from the browser in the container, through SDL2 on a desktop.
- A Linux desktop build: `quake2-linux-x86_64-VERSION.tar.gz` with an
  installer, a launcher that finds a Steam or GOG copy, and ALSA sound.
- A smoke test that builds its own game data, including a one-room map, and
  plays it: the console, a mode change, a save and a load through the game
  library, the sound, a stall, the music and depth 8.
- An Unraid template.
