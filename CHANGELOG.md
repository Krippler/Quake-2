# Changelog

All notable changes are here. The format follows Keep a Changelog, and the
top section's heading is what the release workflow reads: `## [X.Y.Z] — DATE`
on the default branch publishes that version, `## [Unreleased]` publishes only
`edge`.

## [1.3.3] — 2026-10-14

### Added

- **Coloured lighting in the software renderer.** The maps' lightmaps have
  always been in colour -- the orange of base1's fires, the red of alarm
  lights, the blue of the re-release's -- and the GL renderer showed it; the
  software renderer took only each sample's brightness. Now a surface lit in
  colour is lit through the colormap as before, then tinted and taken back
  to the nearest of the palette's 256 colours, and so are coloured dynamic
  lights: muzzle flashes, blaster bolts, rockets. Surfaces lit white are
  drawn exactly as they were. `sw_colorlight 0` turns it off, and a value
  between 0 and 1 keeps that much of the colour.

### Fixed

- **The re-release's messages were still shown as their names**
  (`$map_establish_communication`) when the re-release's pak was mounted
  without the folder it came in: the container looked for `Q2Game.kpf` only
  beside that folder's `baseq2`. It now looks anywhere in the mount, and says
  in its log when there is none -- copy `Q2Game.kpf` from the re-release's
  folder (beside its `baseq2`) into the mount.

## [1.3.2] — 2026-10-14

### Fixed

- **Black bars across the view in the re-release's maps**, level whatever
  the angle and running to the right edge of the screen, and the streaked
  floors that were the same bars with texture in them. The software
  renderer caches each edge for the two faces that share it, one each way;
  the re-release's maps share edges among three and four faces (1712 in
  base1), and the face that lost its edge to the cache was drawn on to the
  edge of the screen. Each such edge now gets a copy of its own as the map
  loads.
- **The help computer and messages showed `$map_establish_communication`**
  and the like: the re-release's maps name their messages and keep the
  words in `Q2Game.kpf`. With `QUAKE2_RERELEASE=1` the container and the
  desktop launcher take `localization/loc_*.txt` out of it, and the game
  shows the text, in the original font (`loc_language`, `english` by
  default).
- **Warping textures larger than 64x64** -- the re-release's lava, crystals
  and windows -- were drawn streaked from past the end of the texture.
- **A crash drawing water or glass with the camera on its surface**, as
  swimming puts it: the translucent polygons' vertices were not kept inside
  the view, and a span row could fall past the end of the frame.
- **Over-long paths** read past their buffer: `Com_sprintf` left a string it
  had cut short unterminated.

## [1.3.1] — 2026-10-13

### Fixed

- **A crash in `R_EmitEdge`, and black bands across the view**, near doors,
  lifts and other moving brushes, most of all in the re-release's maps.
  `R_RecursiveClipBPoly` measured each vertex's distance from a splitting
  plane twice, and with `-ffast-math` the two could disagree for a vertex on
  the plane; the polygon then crossed it once, and the clip edge was made
  from the last call's points. Each distance is now measured one way, and a
  stray crossing is left open rather than joined to stale points. Found by
  rendering every map from thousands of random viewpoints.
- **A brush fragment could write past the end of the frame's edges**: the
  room check counted the face's edges, not the fragment's, which BSP
  splitting makes longer.
- **Faces went undrawn, black, in busy views**: a frame held 2000 edges and
  1000 surfaces, sized for 1997 at 320x240 -- the `sw_maxedges` default was
  meant to be larger, but was written as the string "x". Now 32000 and
  16000, freed with each map rather than leaked.
- **city2 from the re-release would not load** (`PF_setmodel: NULL`): its
  maps have triggers with no brush, fired only by name, which the 1997 game
  code hands the engine as a NULL model. The engine leaves such an entity
  without a model, as the re-release does, rather than stopping the map.
  Every map in the re-release's pak that this engine reads -- 146 of them,
  the base campaign, both mission packs', the deathmatch maps -- now loads
  to a connected player.
- **The title screen's demos stopped playing in 1.3.0**, with or without the
  re-release: the original game's demos are in protocol 26, from its first
  release, and only 34 was accepted. Any 1997 protocol is now; the
  re-release's 2022 demos are still passed over.

## [1.3.0] — 2026-10-13

### Added

- **The 2023 re-release's pak plays on top of the original game's**, with
  `QUAKE2_RERELEASE=1` (the container, the desktop launcher and the Unraid
  template). For each file the engine takes the first copy it can read: the
  re-release's remade maps -- the base campaign, The Reckoning, deathmatch --
  with their new lighting, its models and textures; and wherever its copy is
  missing or past what a 1997 engine reads, the original's, so the console
  font, the menus and the status bar look as they did. Its lighting is
  resampled from the BSPX `DECOUPLED_LM` lump onto the grid the software
  renderer lights from. Call of the Machine (QBSP maps), the N64 campaign
  and the re-release's demos do not play. See DOCKER.md.

### Fixed

- **The re-release's pak alone crashed the engine** in `Draw_Char`, having
  no console font. It now stops at once, saying the original game's paks
  are needed with it.

## [1.2.0] — 2026-10-13

### Added

- **A music volume slider** in Options, under the effects volume. It sets
  `cd_volume`, and replaces "sound compatibility", a Windows-only setting
  that did nothing here.
- **The menus, console and status bar scale with the resolution**, at a whole
  multiple chosen for the screen: three times at 1920x1200, so they are no
  longer a small block in the middle of it. `scr_scale` sets it by hand; 1 is
  the original.

### Fixed

- **Particles were far too big at high resolutions**: id's formula doubled
  their size for every 320 pixels of width, five times too big at 1920. They
  keep the same size relative to the screen now, widescreen included.
- **A pak with more than 4096 files**, such as the 2023 re-release's, stopped
  the engine; it loads now.
- **An error while the game data loads crashed the engine** (SIGSEGV in
  `Master_Shutdown`) instead of saying what was wrong. It says it now.

### Changed

- **Documentation brought up to date with the mission packs:** the Unraid
  template's overview and Game setting, the `QUAKE2_GAME` entries in the
  README, DOCKER.md and the Compose file, the build list in PORTING-NOTES.md,
  and the smoke-test workflow's description name The Reckoning and Ground
  Zero alongside the base game and CTF.

## [1.1.0] — 2026-10-13

### Added

- **The mission packs play.** The Reckoning (`xatrix`) and Ground Zero
  (`rogue`) now run their own game code, so their new monsters, weapons and
  items are there; until now their data ran on the single-player game's code
  without them. The code is Yamagi Quake II's GPLv2 versions, carried
  unmodified in `xatrix/` and `rogue/`; id's own 1998 release of it is under
  a licence that does not allow modified copies. Start on one with
  `QUAKE2_GAME=xatrix` or `rogue` in the container, or
  `quake2 +set game xatrix` on the desktop. Both are in the image and the
  Linux tarball.
- The smoke test loads its map under each mission pack's library and saves
  and loads a game in each.

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
