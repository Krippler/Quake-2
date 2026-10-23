# Changelog

All notable changes are here. The format follows Keep a Changelog, and the
top section's heading is what the release workflow reads: `## [X.Y.Z] — DATE`
on the default branch publishes that version, `## [Unreleased]` publishes only
`edge`.

## [1.3.16] — 2026-10-23

### Fixed

- **The player vanished from the Quake 64 levels' closing fly-over.** The
  camera takes the player out of the world, as an intermission does, and
  left nothing in their place; a figure of them now stands where they
  were, as in the re-release.
- **A Quake 64 level's state was not kept** for coming back to it: its
  name has a directory in it, `q64/outpost`, and its level file could not
  be written under `save/current` ("Failed to open"). Level files are
  named `q64#outpost` now.

## [1.3.15] — 2026-10-23

### Fixed

- **The Quake 64 levels did not end**: the lift at the end of Strogg
  Outpost, and each level's exit after it, went nowhere. They end with
  the N64's camera flying over the level -- the re-release's
  `target_camera`, which the 1997 game had not got -- and it is that which
  goes on to the next level. It flies now, a button skips it, and the next
  level loads (the N64's cutscenes between them are not in the re-release
  and are passed over).

## [1.3.14] — 2026-10-23

### Fixed

- **Stuck on walls, corners, boxes and ladders** since 1.3.13: brushing
  against one could stop the player dead. 1.3.13 counted a player touching
  a wall as clear of it, to free them from Call of the Machine's drop pod,
  which they need not move in; the 1997 rule is back.

## [1.3.13] — 2026-10-23

### Added

- **The re-release's Quake 64 campaign and Call of the Machine.** All 222
  of its maps now play from its own pak, `q64/` and `mgu*` among them
  (`map q64/outpost`, `map mguhub`): the 12 in its extended QBSP format,
  faces wider than the software renderer's 256-unit surfaces (cut into
  pieces as they load), lightmaps on a grid of their own, more than 255
  models (sent past 255 by a protocol extension only this engine speaks),
  and the map limits up to the re-release's.
- **PNG images.** A texture, sky, picture or skin the re-release has only
  as a PNG is read from it, as its TGAs are -- the Quake 64 maps' textures.
- **`trigger_teleport`**, Ground Zero's, in the base game: Call of the
  Machine's first map lands its drop pod with one.

### Fixed

- **The sky smeared in the Reckoning's demo on Outer Compound**, and in
  every map whose sky the re-release has only as a 24-bit TGA -- the
  mission packs', Call of the Machine's, most deathmatch maps'. The
  software renderer read only 8-bit skies and drew nothing in their place.
  It now takes the re-release's TGA wherever the 8-bit image is missing
  -- skies, textures, pictures, skins -- in the palette's colours.
- **A box exactly as wide as a gap no longer sticks in it.** Touching a
  brush counted as being inside it, which left the player unable to move
  at the start of Call of the Machine, in a drop pod exactly their width;
  and a start point with a ceiling less than 9 units above the player now
  lifts them less.
- `+map q64/geo-stat` on the command line went to `q64/geo`: a `-` inside
  an argument was taken for the start of another.

## [1.3.12] — 2026-10-22

### Fixed

- **A flat brown band across the view** at the re-release's Main Gate,
  whenever the lift beside the gate was up: the renderer, cutting the
  lift's faces along the map's walls, lost an edge of its rim where the rim
  lies in the plane of the shaft's diagonal walls, and drew that piece on
  to the edge of the screen.

## [1.3.11] — 2026-10-22

### Added

- **The re-release's lamp lights.** Its remade maps light their rooms with
  real-time lights at the lamps as well as with their lightmaps -- spot
  lights under fittings, flickering ones, some switched by the level -- and
  these were left out, so places lit by them were darker than the
  re-release's. They are drawn now, on walls, floors and monsters, without
  shining through walls; **lamp lights** in Options → Video turns them off.
  The re-release's shadows of moving things are not drawn.

## [1.3.10] — 2026-10-22

### Added

- **The big gun over the re-release's Main Gate fires.** Pressing the gate
  button arms the gate, and the turret above swings round and blasts it
  open, as the re-release plays it; before, the gate simply blew up and
  the gun sat still. That takes Ground Zero's `turret_invisible_brain`,
  the re-release's `func_explosive` that waits to be armed (and the one
  that can always be shot, like the Hangar's fuses), and a turret's
  rockets doing the damage the map gives them.

## [1.3.9] — 2026-10-21

### Fixed

- **Stuck in the re-release's Warehouse** behind a door that "is opened
  elsewhere": its first door opens through a `trigger_coop_relay`, one of
  the re-release's own entities, which the game did not have. It does now
  (in single player it simply passes the signal on; in coop it waits for
  everyone to gather, as the re-release does).
- **Lifts missing from the re-release's Hangar and Main Gate**: they are
  `func_plat2`s, the Ground Zero mission pack's lift, which the base game
  did not have either. It does now.
- **"This door is opened elsewhere 1"** and the like, when `Q2Game.kpf` is
  not in the mount: a message worded from its name lost the number the
  re-release adds to tell names apart; a count ("Airstrike incoming in 5")
  keeps it.

### Changed

- The smoke test measures the tone's pitch only where it sounds. A CI
  runner stalling as the engine started had the mixer skip past most of
  the short test tone, and the silence after it read as a low pitch.

## [1.3.8] — 2026-10-21

### Fixed

- **A crash back to the demo on the way into a level visited before**, in
  a game carried over from an earlier version. A savegame, and the copy of
  each level left behind in `save/current`, keeps the game's functions as
  places in the game library that wrote it; a new build moves them, and a
  door read back from an old copy ran the wrong code. The check id made was
  the build date, which builds made on the same day share. Saves are now
  marked with the build that made them: a savegame from another is refused
  with a message saying so, and a level kept by another starts again as
  if it had not been visited. Saves made before this version are from
  another build, so a game in progress has to be begun again from a unit's
  start (or `map`).

### Changed

- The README and the rest of the documentation brought up to date with
  the re-release support, the demos, coloured light and the mission packs.

## [1.3.7] — 2026-10-20

### Fixed

- **"$g_secondary_mission_objective" and the like** when `Q2Game.kpf` is
  not in the mount: only a map's messages were worded from their names.
  The game's own are now too ("Secondary mission objective: Use sewer
  tunnels to gain"). The words themselves are in `Q2Game.kpf`, in the
  re-release's folder; copy it into the mount for them.

## [1.3.6] — 2026-10-20

### Fixed

- **Doorways showing flat grey in the re-release's maps** (ware2, among
  others), and grey on the far side too. Some of its doors start open, with
  a name to close them by; the 1997 game code opened a door's area portal
  only as the door moved, so the doorway stood open with its portal shut and
  nothing past it was drawn -- the grey is the empty background. A door that
  starts open now opens its portals as it spawns, and closes and reopens
  them the right way round after, as the re-release's own game code
  (id-Software/quake2-rerelease-dll) does. In the base game, The Reckoning,
  Ground Zero and CTF.
- **The original game's ware1 and fact3 would not load** without the
  re-release's pak on top since 1.3.0: a check for maps the engine can use
  turned away any whose bounds reach past 4096 units, and those two do
  (4408, and 99999). It played them in 1997; the check is gone.

## [1.3.5] — 2026-10-20

### Added

- **The re-release's demos play.** They are recorded in its own network
  protocol, 2022, which it has not published and which 1997's engine
  cannot read: the attract loop stopped at "No demo demos/xdemo1.dm2 in a
  1997 protocol to play". The demo server now reads them and writes them to
  the client as 1997 frames as they play -- its 40 frames a second as 1997's
  10, its larger numbers fitted into 1997's limits, its status bar as the
  original's, its messages in words. All seven in its pak play to the end:
  demo1 and demo2, The Reckoning's three, Ground Zero's two. Its monsters'
  newer animations, which the 1997 models standing in for them have not
  got, show their first frame.

### Fixed

- **The attract loop stopped at a demo that is not there.** The
  re-release's loop names a `demo3.dm2` it has not got; a demo that is
  missing or in no protocol the engine plays is now passed over for the
  next, as a missing cinematic always was.

- **"Couldn't find spawn point start" going into the Ammo Depot (bunk1)**
  with the re-release's maps. A level's exit names where the next level
  starts you; the 1997 Outpost asks for bunk1's spawn point `start`, which
  the re-release's bunk1 does not have. When the named spawn point is not
  there, the game now starts you at the level's own start instead of
  stopping. In the base game, The Reckoning, Ground Zero and CTF.

## [1.3.4] — 2026-10-14

### Added

- **"coloured light" in Options → Video**: off, half or full, taking effect
  at once (`sw_colorlight` 0, 0.5 or 1).

### Fixed

- **The help computer still showed `$map_establish_communication`.** The
  words were only taken out of `Q2Game.kpf` when `QUAKE2_RERELEASE=1` had
  found the re-release's pak in `rerelease/baseq2`; a pak put into the game
  any other way left them behind. They are now taken out whenever a
  `Q2Game.kpf` is anywhere in the mount, and the log says where from or why
  not. Without one, a map's message is worded from its name -- "Establish
  communication" -- which is what the re-release names it after.

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
