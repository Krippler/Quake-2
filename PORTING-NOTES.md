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
- **`client/menu.c`**: Options has a music volume slider, for `cd_volume`,
  in the place of "sound compatibility", which set `s_primary`, a DirectSound
  setting that nothing outside Windows ever read.

## Files and errors

- **`qcommon/files.c`**: a pak with more than 4096 files was a fatal error.
  4096 was id's pak tool's limit, not the format's, and later paks -- the
  2023 re-release's among them -- have more. The directory is read onto the
  heap at the size it gives, once that is checked against the file, and a
  name that fills all 56 bytes no longer runs into the next field.
- **`server/sv_main.c`**: a fatal error while the file system starts, a pak
  that is not one for instance, shuts the server down before `SV_Init` has
  made the `dedicated` cvar; `Master_Shutdown` read it through a null
  pointer, and the error's message was lost to a crash.
- **`game/g_save.c`, `ctf/g_save.c`**: a savegame keeps function pointers
  as offsets from `InitGame`, which hold only for the library that wrote
  them. id checked `__DATE__` in a savegame and nothing in a level file, so
  a rebuild the same day loaded old offsets and a door's `think` jumped into
  other code (`Think_CalcMoveSpeed`, coming back to a level). Both now carry
  a hash of the game library's own file (found with `dladdr`) where the date
  and the base pointer were: a savegame from another build is refused with
  `gi.error`, and a level file from one is passed over -- the level stays as
  `SpawnEntities` made it, its entities linked again after `SV_ClearWorld`.
  The mission packs' code (Yamagi's) saves functions by name and needs none
  of this.

## The 2023 re-release's data

The re-release's `rerelease/baseq2/pak0.pak` holds the same paths as the
original game's -- 4466 `.wal` textures, 370 `.md2` models, 163 `.pcx`
pictures, 222 maps -- beside PNG, TGA and MD5 versions for its own engine,
and lacks `pics/conchars.pcx`. With it on top of the original's paks
(`QUAKE2_RERELEASE=1`, as `baseq2/pak9.pak`):

- **`qcommon/usable.c`** (new, built into the engine and the renderer):
  whether a copy of a file is one this engine reads -- a map in IBSP 38 or
  the re-release's QBSP within the loaders' limits, its edges' vertices and
  its faces' texinfo and edges in range; an MD2 within the 1997 limits; an
  8-bit PCX; a WAL with its mip levels inside it. `MAX_MAP_LIGHTING` is not
  checked: nothing loading a map enforces it, and the re-release's lighting
  lumps are past it. Nor is how far a map reaches: the 1997 game's own ware1
  goes to 4408 units and fact3's bounds to 99999, and both play. (1.3.0 to
  1.3.5 did check it, against 4096, and turned those two away.)
- **`ref_soft/r_image.c`**: some of the re-release's images are only there
  as 24- or 32-bit TGA or as PNG: 84 skies (the mission packs', Call of
  the Machine's, the deathmatch maps') and the Quake 64 maps' textures. A sky
  was asked for as a `.pcx` and not found, and the software renderer then
  drew nothing where it was -- the last frames smeared across the top of
  the view, in the Reckoning's demo on xcompnd1 for one. A `.pcx` or `.wal`
  that is not there is now taken from the `.tga`, else the `.png`, of the
  same name (`r_png.c`, new: every colour type and bit depth, `tRNS` and
  Adam7, inflated by zlib), the TGA read by
  a `LoadTGA` that checks its lengths (id's did not, and stopped the game
  on a kind it did not read), each pixel the nearest of the palette's
  colours that the colormap shades (the coloured light's table), a sky
  averaged to 256x256 as the originals are, a texture to a multiple of 16
  on each side with four mip levels made by averaging.
- **`qcommon/files.c`**: `FS_LoadFileSkip` and `FS_FOpenFileSkip` open the
  copy of a file a given number of copies down the search path;
  `FS_LoadUsableFile` walks down until `usable.c` accepts one, and says why
  it passed over each. `CM_LoadMap` uses it, the renderer the same through a
  new `refimport_t` field (`client/ref.h`, at the end), for maps, models,
  pictures and textures: so the console font and the soldier with 575
  frames each come from the original's paks. Server, client and renderer
  settle on the same copy of a map, and its checksum agrees.
- **The re-release's maps past 1997's**: the Quake 64 campaign, Call of
  the Machine, `q2dm3` and four of Ground Zero's.
  - `qcommon/qfiles.h`: QBSP, "QBSP" for "IBSP", is IBSP with ints for its
    shorts and floats for its nodes' and leafs' bounds (`dqnode_t` and the
    rest); `MAX_MAP_*` up to the re-release's own. `cmodel.c` and the
    renderer's `r_model.c` read either; `medge_t` and the leaf lists are
    ints for it.
  - `ref_soft/r_model.c`: faces wider than the surface cache's 256 texels
    (`q2dm3`'s, the N64 maps' floors) are cut along the texture's 240-texel
    grid lines as the map loads, `Mod_SplitSurfaces`, each piece a face of
    its own with its share of the lightmap; the leafs' and nodes' face
    lists are remapped to the pieces. The re-release's `DECOUPLED_LM`
    lightmaps, on a grid of their own per face, are resampled on to the
    16-unit one this renderer lights by. A translucent face that flows
    (Ground Zero's `SURF_FLOWING`) is drawn as turbulent ones are, its
    extents being theirs (`r_poly.c`).
  - more than 255 models (Call of the Machine's mgu5m2 has 227 brush
    models, and its monsters' and items' on top): `MAX_MODELS_EXT`, 1024, with the numbers past 255 in
    configstrings of their own after 1997's, `CS_MODEL(i)`; an entity with
    one is sent with `U_MODEL16` and its model numbers as shorts, a player
    state with `PS_WEAPONINDEX16`. 255 itself is never handed out, a
    client drawing it as a player in their skin. A 1997 client cannot read
    these, but none is asked to: they are only sent for a map past 255.
    The level files of a save keep 1997's layout with the new configstrings
    after the portal state, so a save from before them loads.
  - `game/p_client.c` lifts a start point by less than 9 where 9 is in
    the ceiling, as the re-release does, and the base game has Ground
    Zero's `trigger_teleport` and `info_teleport_destination`, which
    mgu1m1's drop pod lands by. The pod is exactly as wide as the player,
    and touching a brush counts as inside it (`CM_TestBoxInBrush`), so the
    player cannot move in it -- nor need to. 1.3.13 counted touching as
    outside, to free them; but pmove snaps to an eighth of a unit, which
    then could leave a player exactly on a wall, and a trace starting there
    along or into it is all solid: they stuck on walls, corners and ladders.
  - `game/g_target.c`: the re-release's `target_camera`, which ends every
    Quake 64 level and mgu6m3: used, it holds the players in an
    intermission (`level.camera`) and flies their view through the points
    it targets, at each one's speed and after each one's wait, facing its
    `pathtarget`; then it uses -- not removes -- what its `killtarget`
    names, the level's `target_changelevel`, whose intermission keeps the
    camera's last view. A button after two seconds skips the flight, and a
    figure of each player -- their entity state, standing -- is left
    where they were for the camera to see. The
    `.cin` each changelevel names first is not in the re-release; the
    client finds none and goes straight on to the map after the `+`.
  - `game/g_items.c`: Quake 64's keys, `key_explosive_charges`,
    `key_yellow_key` and `key_power_core`, at the end of the item list so
    that 1997's items keep the numbers saved games hold. Its planted
    charges keep `ITEM_TRIGGER_SPAWN|ITEM_NO_TOUCH`, which `SpawnItem`
    clears from 1997's keys.
  - The rest of the re-release's Quake 64 entities, from its game source
    (id's quake2-rerelease-dll, GPL): `target_gravity`,
    `target_killplayers`, `target_soundfx`, `target_light`,
    `misc_hologram`, `misc_lavaball`, `monster_tank_stand`,
    `item_invisibility`; `target_poi` (its compass) and `misc_flare` (its
    renderer's) are dropped. `target_light` is a light carried by an
    entity, `RF_CUSTOMLIGHT` (`q_shared.h`), radius in `frame` and colour
    in `skinnum`, which the client adds as a dynamic light and does not
    draw (`cl_ents.c`); the game keeps the light styles, `g_lightstyles`,
    to fade it by, with the re-release's N64 styles 12 to 14.
    `level.is_n64` for a map in `q64/`: powerups used when picked up,
    `misc_teleporter_dest` not shown, no `G_TouchTriggers` from a monster
    in the first frame. Worldspawn's `goals` are the help computer's
    message, one tab-separated `$key` after another, each `target_goal`
    going on to the next (`G_SetGoal`, `G_SetClientSound`).
  - `game/g_cotm.c` (new): Call of the Machine's targets and triggers,
    from the re-release's source and Ground Zero's:
    `target_crossunit_trigger`/`_target` (the hub's unit locks, in
    `game.cross_unit_flags`), `trigger_health_relay` with a monster's
    `healthtarget` (used from `T_Damage` each time it is hurt) and
    `health_multiplier` (`monster_start`), `target_anger`, `target_music`,
    `target_sky` (the client takes up a sky changed mid-level,
    `cl_parse.c`), `target_story` (centre-printed), `target_steam`,
    `func_eye`, The Reckoning's `misc_nuke`, and `monster_makron` in the
    spawn table; `target_healthbar`, `target_autosave`, `trigger_fog` and
    `trigger_flashlight` are dropped, `func_animation` is a still brush.
    A class with no spawn function that `G_StandIn` has a stand-in for is
    spawned as that, from `ED_CallSpawn`.
  - `game/g_xatrix.c` (new): The Reckoning's weapons its monsters fire,
    from its code -- the ion ripper (`MOVETYPE_WALLBOUNCE`, in
    `g_phys.c`), the blue hyperblaster, the laser soldier's beam
    (`monster_dabeam`), the phalanx's plasma. `m_soldier.c` has its
    ripper, hyperblaster and laser soldiers as the re-release does: style
    1, skins 6 to 11 of the soldier model, `SOLDIER_SKIN` giving each as
    the soldier it takes after for what goes by skin; `m_gladiator.c` its
    phalanx gladiator, skin 2. `MAX_FRAMES` is 1024 (`qfiles.h`): the
    re-release's soldier has 575 frames, its gunner 799; every 1997
    monster's frames are the first of its re-release model's, so the 1997
    code drives them as it did. Its gekk is `m_gekk.c`, its code with its
    acid gibs, and its super tank `monster_boss5` in `m_supertank.c`: a
    power shield and `fire_heat`'s seeking rockets. `AI_IGNORE_PAIN`
    keeps a leaping gekk from its pain (`T_Damage`).
  - `game/g_rogue.c` (new): Ground Zero's, from its code (Yamagi's GPL
    port): blaster2, flechettes, the tracker, the heat beam, the tesla
    mine and its bad areas, `fire_player_melee`, summoning
    (`FindSpawnPoint`, `CheckGroundSpawnPoint`, `CreateGroundMonster`,
    `CreateFlyMonster`, `SpawnGrow_Spawn`), the stationary monster start,
    and the plat and jump checks for a monster's `monsterinfo.blocked`,
    which `SV_NewChaseDir` calls before it turns aside (`m_move.c`). Its
    monsters: the daedalus (`m_hover.c`, style 1), the wall turret
    (`m_turret.c`), the medic commander (`m_medic.c`, by mass), the
    stalker (`m_stalker.c`) and the carrier (`m_carrier.c`), with the
    kamikaze flyer and the circle strafe in `m_flyer.c`. The AI takes up
    what they need and nothing else changes for 1997's monsters: blind
    fire (`AS_BLIND`, `ai_checkattack`), `AI_MANUAL_STEERING` (the yaw is
    set, not sought), `AI_IGNORE_SHOTS`, `AI_DO_NOT_COUNT`, `AI_CHARGING`
    and `AI_CIRCLE_STRAFE` (`ai_charge`, which only Ground Zero's
    monsters set), and `gravityVector` for the stalker's ceilings in the
    step physics, ground checks and `M_droptofloor` -- unset, it is down.
    A tesla's field becomes a bad area once it zaps a monster
    (`M_ReactToDamage`), which `SV_movestep` keeps monsters out of.
  - `game/g_rr.c` (new): the re-release's (GPL, ZeniMax) for its monsters,
    ported to the 1997 API: box-to-box reach (`range_to`, `fire_hit_rr`),
    `PredictAim`, `M_CheckClearShot`, the lob pitch search, the slam,
    ducking and dodging (`M_MonsterDodge`, given where a shot will hit by
    `check_dodge`), and the reinforcements its summoners use (a map's
    `reinforcements` and `monster_slots`, held inline in `monsterinfo` so
    that saved games keep them). Its monsters: the arachnid
    (`m_arachnid.c`), the shambler (`m_shambler.c`) and the gunner
    commander (`m_guncmdr.c`, drawn at the gunner's size, as this
    protocol has no scale). Its muzzle flashes 211 to 250 are in
    `m_flash.c` and the client's; the shambler's model beam the client
    draws as tiled model segments (`CL_ModelBeam`), and its slam is
    `TE_BERSERK_SLAM`.
  - The mission packs' weapons and powerups, at the end of the item list:
    the phalanx and mag slugs (The Reckoning's), the chainfist, the
    disruptor and rounds, the plasma beam and the tesla mine (Ground
    Zero's, with the re-release's one view model for the beam and the
    tesla), double damage and DualFire. Weapons multiply their damage by
    `damage_multiplier` (quad 4, double damage 2), and under DualFire
    `Weapon_Generic` runs a weapon's frames twice a frame.
  - `ref_soft/r_image.c`: a skin is allocated with two rows and more
    either side of it (`R_AllocPixels`): drawing an alias model steps a
    texture pointer along each span, and where a triangle is clipped at
    the screen's edge it can land outside the skin, which ASan caught
    with the gekk's.
  - `client/cl_loc.c`: the re-release's key hints, `%bind:cmd:text%`
    before a message's words, are left out.
  - `server/sv_ccmds.c`: a level's files in `save/current` are named by
    `SV_LevelFileName`, with `#` for the `/` of `q64/outpost`: the save
    slots are flat directories, copied and wiped by `*.sav`.
  - `qcommon/cmd.c`: on the command line `-` starts another command only
    where an argument would, not inside `q64/geo-stat`.
- **`server/sv_game.c`**: `PF_setmodel` given no model leaves the entity
  without one instead of stopping the map. The re-release's maps have
  point-sized triggers fired only by name (city2's `trigger_once`), which
  the 1997 game code passes on as a NULL model; this covers the base game,
  CTF and the vendored mission packs alike without touching their code.
- **`server/sv_user.c`**, **`server/sv_rrdemo.c`** (new): the re-release's
  demos are in its own protocol, 2022, which it has not published; what
  `sv_rrdemo.c` reads of it was worked out from the seven demos in its pak0,
  every message of which it reads without a byte left over, and its header
  sets out the format. The demo server sends the client a message every
  100 ms; a re-release demo is 40 frames a second, deltas of each other, with
  numbers past 1997's limits. So the state its messages build up is kept,
  and each 100 ms a 1997 frame of it is written, delta compressed against the
  last, through the server's own `MSG_WriteDeltaEntity` and
  `SV_WritePlayerstateToClient`, with the sounds, effects and prints of the
  frames in between. Its configstrings go to 1997's places, models, sounds
  and pictures past 255 to numbers left free, its status bar (drawn with
  commands a 1997 client has not got) is 1997's single-player one, and its
  prints of words to look up go to `CL_Localize` with their arguments. The
  original game's demos are in 26, from its first release, and play as they
  are, as the 3.21 client meant them to. A demo in neither, or not there, is
  passed over for the next in the loop of demos, as a missing cinematic is
  (the re-release's `default.cfg` names a `demo3.dm2` it has not got); a
  loop with nothing in it to play stops at the console.
- **`client/cl_fx.c`**: a monster's muzzle flash numbered past the end of
  `monster_flash_offset` is ignored rather than read from past it.
- **`ref_soft/r_alias.c`**: a frame or skin a model has not got is said in
  developer mode only. The re-release's monsters animate past the end of
  the 1997 models that stand in for theirs, every frame of its demos.
- **`ref_soft/r_model.c`**: the re-release's maps leave every face's
  `lightofs` at -1 and keep their lighting in a BSPX lump, `DECOUPLED_LM`:
  per face, a lightmap of its own size (8 units a texel) and a matrix from
  world space to it. `Mod_LoadDecoupledLightmaps` resamples each onto the
  16-unit grid at the face's `texturemins` that the surface cache and
  `R_LightPoint` index: each grid point is taken back onto the face's plane
  and read bilinearly from the re-release's lightmap. Its see-through grates
  are not drawn as such; its real-time lights are, below.
- **`ref_soft/r_model.c`**: the renderer caches an edge once a frame for
  the two faces that share it, one winding each way; id's compiler never
  shared one more widely. The re-release's maps do (1712 uses in base1, 0 in
  the original), and a face handed another's cached edge was drawn on to
  the right edge of the screen, black. `Mod_CountSharedEdges` and
  `Mod_LoadSurfedges` give every use past the first each way an edge of its
  own.
- **`ref_soft/r_scan.c`, `r_poly.c`, `r_edge.c`**: the warping surfaces'
  `& 63` and `<< 6` took every liquid to be 64x64; the re-release's lava and
  crystals are 128 and 256. Masks and row length now come from the texture.
- **`client/cl_loc.c`** (new): the re-release's maps give `$map_...` keys
  for their messages; the words are in `localization/loc_*.txt` in its
  `Q2Game.kpf`. `CL_Localize` replaces each key in centre prints, console
  prints and the help computer's layout; UTF-8 punctuation is folded to the
  1997 font's ASCII. A key the file has not got, or every one when there is
  no file, is worded from the key itself, which the re-release made from the
  message's first words: `$map_establish_communication` as "Establish
  communication", `$g_secondary_mission_objective` as "Secondary mission
  objective". A key's arguments, as its demos' prints carry them, go into
  the text's `{}`, or after the words made from a key.
- **`game/g_func.c`**, **`ctf/g_func.c`**, **`xatrix/src/g_func.c`**,
  **`rogue/src/g_func.c`**: some of the re-release's doors start open, with
  a name to close them by (ware2's tank door). The 1997 game opened a door's
  area portals only as it moved -- at spawn only for a door with neither a
  name nor health -- so the doorway stood open with its portal shut and
  nothing past it was drawn: flat grey, the empty background. As the
  re-release's own `g_func.cpp` does, a door that starts open opens its
  portals as it spawns (`Think_DoorActivateAreaPortal`), closes them on
  reaching its closed end and opens them as it goes back. The mission packs'
  savegame tables list the new think.
- **`game/g_trigger.c`**, **`game/g_func.c`**: two of the re-release's
  entities hold its remade maps together. `trigger_coop_relay` -- a
  `trigger_relay` that in coop waits for every player to stand in its brush
  -- is how ware2 opens its first door, so that door stayed shut and the
  level could not be finished. `func_plat2`, Rogue's lift, is hangar1's and
  jail1's lifts, which were not there at all. Both are in the base game now,
  `func_plat2` from `rogue/src/g_func.c` with the re-release's changes
  (spawnflag 8 is `START_ACTIVE`; a trigger's call is not held back; a
  blocker killed does not turn it round). What the 1997 game still passes
  over is the re-release's decoration and bot hints (`target_poi`, `info_landmark`, `info_nav_lock`, `func_animation`,
  `misc_model`, `misc_player_mannequin`).
- **`game/g_misc.c`**, **`client/cl_fx.c`**, **`ref_soft/r_light.c`**: the
  re-release's real-time lights, a `dynamic_light` at each lamp (or
  `shadowlight` keys on a `light`), up to 69 a map: radius, intensity, a
  cone toward a target, a light style. The game gives each to the client in
  a configstring of its own from `CS_SHADOWLIGHTS` (the base game's unused
  general ones), emptied while a named one is switched off; the client
  passes those in the view's PVS, nearest first, to the refresh, flickering
  and fading as the map asks; the refresh adds them to the lightmaps of the
  surfaces each can see and to models. 1997's dynamic lights go through
  walls; these do not: the first time a light is in view, lines through the
  BSP from it to points of each surface in its reach say which it lights,
  kept until the map changes. Nor are they relit every frame, as 1997's
  are: a surface's cache entry is marked with which lamps are on it and how
  bright, and kept until that changes, so a steady lamp costs nothing after
  the first frame. Shadows of moving things, the re-release's, are not
  drawn.
- **`game/g_turret.c`**, **`game/g_misc.c`**: jail1's big gun.
  `turret_invisible_brain` drives a `turret_breach` with no one in it; the
  re-release's version, which can fire without seeing its mark (spawnflag
  1) -- jail1's aims at a point behind the gate it is to break. It aims
  only at its killtarget; where Rogue's would then look for another enemy
  (through `FindTarget`, which a brain with no monster functions cannot
  survive) it stops. The gate is a `func_explosive` with the re-release's
  INACTIVE (8): the button arms it, the rocket breaks it; ALWAYS_SHOOTABLE
  (16) came with it, set on hangar1's fuses. No 1997 map sets either. A
  breach's `count` is its rockets' damage, and a brain's rockets are the
  player's who set it off, as in the re-release.
- **`game/p_client.c`**, **`ctf/p_client.c`**, **`xatrix/src/player/client.c`**,
  **`rogue/src/player/client.c`**: a level's exit names where the next
  starts the player; the 1997 Outpost asks for bunk1's `start`, which the
  re-release's bunk1 has not got, and `SelectSpawnPoint` stopped with
  "Couldn't find spawn point start". Without the named one it takes the
  level's untargeted start, or any.
- **`docker/entrypoint.sh`**, **`desktop/quake2`**: `QUAKE2_RERELEASE=1`
  links `rerelease/baseq2/pak0.pak` as `baseq2/pak9.pak`, and takes it away
  again when it is unset. Whenever a `Q2Game.kpf` is in the mount, however
  the re-release's pak got into the game, its localization files are taken
  out into `baseq2/localization`.
- **`qcommon/pmove.c`**, **`qcommon/cmodel.c`**: on the re-release's own
  maps the player moves as its `p_move.cpp` moves them, ported to C beside
  the 1997 code, which is left as it was for every other map (scripted
  runs through both on the same maps match the old build frame for frame).
  A map is the re-release's when its BSP has the `DECOUPLED_LM` lump only
  the re-release's lighting has, or is under `maps/q64/`; `CM_LoadMap` says
  so in `pm_rerelease`, and as the server and the client's prediction both
  load the map through it they cannot disagree, with no change to the
  protocol. `pm_n64` is the Quake 64 levels in single player. What it does
  differently: stepping down stairs and slopes on the ground; a step's
  edge not stood on mid-air; a slope wedged against a wall stood on; out
  of a wall the shortest way when stuck (`G_FixStuckObject_Generic`);
  ladders climbed with jump or forward unless looking down, descended with
  crouch or back, crossed with strafe, and none under water; swimming up
  and down with jump and crouch, no sinking while standing on the bottom,
  crouched swimming at crouch speed, and a waterjump only where there is
  somewhere dry to land, tried out first; crouching in the air; a landing
  pause only crouched or on the N64, and a trick jump's window
  (`PMF_TIME_TRICK`, the free bit 128 of `pm_flags`) when landing on a
  ledge still rising; `sv_airaccelerate` as an acceleration of its own; and
  flying twice as fast as spectator or noclip. Its full float positions are
  the network's eighths here, and its `trace.surface2` (two planes hit at
  once) is not in this engine's traces, so that experiment of its is left
  out. The engine sets `sv_rerelease_map` for the game, whose players on
  such a map are not heard jumping by monsters but are heard landing hard.
- **`game/p_weapon.c`**, **`game/g_weapon.c`**, **`game/p_view.c`**: on
  the same maps (`RR_Map`) the player's weapons are the re-release's
  `p_weapon.cpp` and `g_weapon.cpp`. `P_ProjectSource` turns a shot toward
  where a line from the eye along the view first hits, unless that is a
  monster or player within 128 units or the eye is in a wall, which is
  every weapon's through the one function, the mission packs' included.
  Kicks a weapon sets in a frame become the start of a 200 ms decay
  (`rr_kick_*`, `kick_time`, `kick_total`; the BFG's lasts `DAMAGE_TIME`
  through `kick_length`) instead of being cleared at the frame's end, so
  at this 10 Hz a kick is whole, then half, then gone, as the re-release's
  is on any tick rate. Ammo goes through `P_RemoveAmmo`, which plays its
  `weapons/lowammo.wav` at its items' `quantity_warn`. Its weapon state
  machine, rewritten for 40 Hz (buffered fire, `weapon_think_time`),
  behaves as 1997's at 10 Hz and is not taken; nor are its weapon chains,
  instant or quick switching, or lag compensation.
- **`game/m_*_rr.c`**: on the same maps the 1997 monsters are the
  re-release's, ported whole from its `m_*.cpp` into a file of their own
  beside 1997's, whose spawn function hands over to it on such a map.
  Its models have the frames these need, after 1997's, and the maps'
  models are its models. Each takes from `g_rr.c` what the re-release's
  monsters share: `ThrowGibs` with its own gibs (`GIB_SKINNED`, and
  `GIB_HEAD` for the monster itself), `M_CheckGib` by `gib_health`,
  `M_ShouldReactToPain`, `monster_footstep`, `monsterinfo.setskin` for
  the hurt skin (called after damage and death in `g_combat.c`, and in
  the save fields). What it does at 40 Hz is done at 10, its `M_SetAnimation`
  is `currentmove`, its sidestep scale on a move is not taken, and its
  monsters do not find their way with its navigation files, which its
  closed engine reads. So far: the soldier, with The Reckoning's guards
  (`m_soldier_rr.c`), whose prone fire is the re-release's muzzle flashes
  251 to 255; the infantry (`m_infantry_rr.c`), with its jumps
  (`blocked_checkjump_rr`, by `drop_height` and `jump_height`); the
  gunner (`m_gunner_rr.c`, on the gunner commander's frames); the
  berserker (`m_berserk_rr.c`), whose leap's landing slams by its touch,
  as there, its velocity cleared there for its `FL_KILL_VELOCITY`; the
  gladiator (`m_gladiator_rr.c`, on 1997's frames, which are the same),
  which is The Reckoning's plasma gladiator too by its classname, as
  there by `style`; the parasite (`m_parasite_rr.c`, with its jump
  frames after 1997's in `m_parasite_rr.h`), whose proboscis is an
  entity of its own (`proboscus`, saved), a tip that flies and sticks
  and a segment drawn as a model beam from the parasite to it, placed by
  the re-release's `postthink`, which `G_RunEntity` calls after an
  entity's physics. Its `GIB_UPRIGHT` is taken and needs nothing, as
  1997's `gib_touch` lands an organic gib level; the mutant
  (`m_mutant_rr.c`, its jump frames after 1997's in `m_mutant_rr.h`),
  without its `combat_style`, which this game's AI has no use for. A
  monster of the re-release's `AI_STINKY` (the infantry, the mutant) is
  given flies by `monster_dead` every time (`M_FliesLater`), and keeps
  the flag through `monster_death_use`, as there; the tank and tank
  commander (`m_tank_rr.c`, on 1997's frames), whose Quake 64 guardian
  has its health but is not drawn half as large again, there being no
  `s.scale` here, and whose heat seekers are The Reckoning's, without
  the re-release's turn rate (`accel`); the medic and medic commander
  (`m_medic_rr.c`, on 1997's frames, summoning with `m_medic.c`'s
  reinforcements, which are the re-release's), with its patients' own
  `healer`, `badMedic1` and `badMedic2` (saved) and `medicTries`,
  `cleanupHealTarget`, and on these maps `Killed` and `M_ReactToDamage`
  freeing a medic's patient (`M_AbandonHeal`) and the latter's
  `react_to_damage_time`. Its check for room to raise a corpse traces
  down a unit onto it, as this engine's trace that does not move has a
  box resting on the floor in it, as the re-release's does not;
  the brain (`m_brain_rr.c`, on 1997's frames), whose eye lasers are the
  re-release's lasting ones (`monster_fire_dabeam_rr`, its `beam` and
  `beam2`, placed by `postthink`), not The Reckoning's of a frame.
  The iron maiden (`m_chick_rr.c`, on 1997's frames) is The Reckoning's
  heat-seeking one too (`monster_chick_heat`, spawned now: elsewhere
  an iron maiden in its skin).
  The flyer (`m_flyer.c`'s kamikaze too, in `m_flyer_rr.c`) flies by
  the re-release's flight (`AI_ALTERNATE_FLY`, `SV_alternate_flystep` in
  `g_rr.c`, and its `fly_*` fields): its moves steer its velocity to a
  place about its enemy, which its physics carries it by without
  friction, and its AI calls the move each frame though the frame moves
  it no distance. Its numbers are for its 40 Hz frames; at 10 each frame
  gains four of its speed-ups and keeps its old heading as four of its
  turns would. The technician (`m_float_rr.c`) and the icarus and
  Ground Zero's daedalus (`m_hover_rr.c`) fly by it too, and the
  barracuda shark (`m_flipper_rr.c`) swims by it; a swimmer of it coasts
  when not all under water, and falls by gravity then as there, where
  1997's has gravity only out of the water.
  The super tank (`m_supertank_rr.c`, with The Reckoning's `boss5`)
  dies by the re-release's `BossExplode` (`BossExplode_rr`): a thing of
  its own bursting explosions over it till it turns to its gibs, its
  unlit ones here the ones without particles (`TE_EXPLOSION1_NP`).
  So does the hornet (`m_boss2_rr.c`), whose sinking as it dies is
  its `gravityVector` scaled, here its `gravity`, and whose gibs drawn
  larger there are drawn at their own size.
  So does the Jorg (`m_boss31_rr.c`), whose loop while it fires (its
  `weapon_sound`) is its `s.sound`; it throws out the Makron of
  `m_boss32_rr.c` (`MakronToss_rr`).
  The insane marine (`m_insane_rr.c`) is the re-release's but crucified,
  which hangs as 1997's does, the re-release's stationary monsters
  being none of this game's.
- **The mission packs' monsters, the re-release's**: likewise, the
  gekk (`m_gekk_rr.c`, on The Reckoning's frames), which swims by the
  re-release's flight and gibs by `ThrowGibs` into acid gibs
  (`GIB_ACID`: the green trail, full bright).
  Their ducks come down from `monsterinfo.base_height`, which
  `monster_start` sets to every monster's standing height, as there.
- **Muzzle flashes past 255**: the re-release's monsters have 289, all
  numbered here as there (`game/q_shared.h`, `game/m_flash.c`). Those
  past a byte go in `svc_muzzleflash3` (21), as the re-release sends them,
  with the flash a short; `monster_muzzleflash` picks which for every
  monster weapon, and the re-release demo player passes them all on.
- **`client/cl_fx.c`**: footsteps on the re-release's maps are by material,
  as its engine plays them: a trace down to the surface stepped on, its
  texture's `textures/<name>.mat` naming the material (cached per map),
  and `player/steps/<material>1..4.wav`. A monster's step is the new
  entity event `EV_OTHER_FOOTSTEP`, at `ATTN_IDLE`.

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

**`ref_soft/r_misc.c`, `r_part.c`**: a particle's size was `izi >> (8 -
width/320)`, doubling for every 320 pixels of width: right at 320 and 640,
and at 1920 wide more than five times too big for the screen. It is in
proportion to the view now, and to its height, so a widescreen mode does not
make particles bigger either; at 320x240 every number is what it was.

**`ref_soft/r_draw.c`, `r_main.c`, `linux/rw_x11.c`**: the 2D layer -- menus,
console, status bar -- was drawn a screen pixel to a pixel of its art, so the
higher the resolution the smaller it got. It is drawn at a whole multiple,
`scr_scale`: the client is told the screen is that many times smaller, lays
it out as it always has, and the renderer multiplies each `Draw_` call and
the 3D view's rectangle back up. 0, the default, is the largest multiple that
leaves 640x400 of room (3 at 1920x1200, 2 at 1280x720); 1 is id's.

**`ref_soft/r_bsp.c`**: `R_RecursiveClipBPoly`, which cuts a moving brush's
faces up along the world's BSP planes, worked out a vertex's side of a plane
once as the end of one edge and again as the start of the next. Built with
`-ffast-math` the two need not agree for a vertex on the plane, which the
re-release's finely split maps have many of; the face then crossed the plane
once, and the edge closing it was made from the entry and exit points of
whatever call had last set them -- static, never reset. The result was a
crash in `R_EmitEdge` or scanlines of garbage. Distances are taken through
one non-inlined function, the points are reset each call, and a face that
crosses once is left without the closing edge. Found by rendering every map
from thousands of random viewpoints in an optimised build.

A face lying in a splitting plane that is not along an axis -- the rim of
jail1's lift, risen into the plane of the diagonal walls of its shaft --
came out a few ten-thousandths either side of it at each vertex, and so
crossed it four times; the closing edge joins one pair, and the piece left
open drew its spans on to the right of the screen, a flat band of the
rim's colour across the view whenever the lift was up. Within a hundredth
of a unit a vertex is on the plane, as one on a plane along an axis
already was exactly.

**`ref_soft/r_poly.c`**: `R_ClipAndDrawPoly` projected the translucent
surfaces' vertices without keeping them inside the view as `R_EmitEdge`
does; a vertex a fraction past the bottom gave a span row past the end of
the frame, with the camera on a water surface.

**`game/q_shared.c`, `ctf/q_shared.c`**: `Com_sprintf` cut an over-long
string short with `strncpy`, which does not terminate it.

**`ref_soft/r_rast.c`**: `R_RenderBmodelFace` checked there was room for the
face's edges, not the fragment's, which the splitting above makes longer.

**`ref_soft/r_main.c`**: `sw_maxedges` defaulted to `STRINGER(MAXSTACKSURFACES)`,
which is the string `"x"`, so 0, so `MINEDGES`' 2000; `sw_maxsurfs` to the
1000 on the stack. Busy views in the re-release's maps ran out, and the faces
left over were not drawn. 32000 and 16000 now, on the heap, and the last
map's freed rather than leaked.

**`ref_soft/r_light.c`, `r_surf.c`, `r_model.c`**: coloured lighting,
`sw_colorlight`. id's software renderer kept the brightest channel of each
lightmap sample and lit through the colormap, which has 64 levels of light
for each of the 256 colours but no tints. The lightmaps are kept in colour
too now, and a surface with colour in its lightmap, or under a coloured
dynamic light, is lit in red, green and blue: the brightest channel still
picks the colormap's level, then the lit texel is multiplied by each
channel's share of that and taken back to the palette through a 64x64x64
table of nearest colours, made once at start-up. The colours the colormap
never changes -- the fullbrights -- are left alone. Surfaces lit white take
the old path and come out byte for byte as before.

**`client/cl_view.c`**: Hor+. The fov is the horizontal angle of a 4:3
picture; wider than that, the vertical angle is kept and the sides are added,
rather than the top and bottom cropped.

## The mission packs

`xatrix/` (The Reckoning) and `rogue/` (Ground Zero) are not id's code as
released. id published their game source in 1998 (`xatrixsrc320`,
`roguesrc320`) under its Limited Program Source Code License, which does not
allow distributing modified copies and is not the GPL. Yamagi Quake II's
versions of both are distributed under GPLv2, are already 64-bit clean, and
keep id's game API (version 3), so they are carried here as upstream has
them -- `src/`, `LICENSE`, `README.md` and `CHANGELOG`, with `UPSTREAM`
naming the repository and commit -- but for the two changes the 2023
re-release's maps need, which the base game has too and which are set out
above: a door that starts open opens its area portals
(`src/g_func.c`), and a spawn point a level's exit names that is not there
gives way to another (`src/player/client.c`).

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
