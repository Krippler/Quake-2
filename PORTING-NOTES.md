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
- **`linux/cd_stream.c`**, **`client/cl_parse.c`**: the track is what
  `CS_CDTRACK` says, a number or (the re-release's worldspawn "music") a
  file's name, which `CDAudio_PlayName` looks for as it is or with each
  extension; `cd play` and `cd loop` take either. A number is also looked
  for as a bare `NN.<ext>`, as Yamagi's and Quake II RTX's rips are named.
  The re-release's numbering (its mission packs' 12 to 21, Quake 64's 64
  to 79) is played as it is. A looping track plays `cd_loopcount` times (4)
  and then `cd_looptrack` (11) for good, as id's Windows CD player had it
  (`cd_win.c`), the switch made in `CDAudio_Update`, not in the middle of
  mixing; or, if there is no loop track, the same for good. The
  re-release's `CS_CD_LOOP_COUNT` (here `CS_HEALTH_BAR_NAME+2`, a map's
  `loop_count`, 0 on Quake 64's) overrides the count, 0 for good; that
  its empty default is the 1997 player's is taken, not known. Worldspawn's
  "music" goes with its "sounds" ("file number"), and the client plays the
  number if it has not the file: ndctf0's `D_DDTBLU.ogg` is in no release. The game sends `target_goal`'s "sounds" when the last goal is
  done (unless KEEP_MUSIC) and `target_camera`'s when it starts, as the
  re-release does. `cd_linux.c`'s drive has numbers only.
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
    `item_invisibility`, `misc_flare` and `target_poi`. A flare is sent
    as `target_light` is, model 1 and nothing of it drawn, `RF_FLARE`,
    its colour in `skinnum` (an "rgba" key of four numbers is taken as
    the re-release takes it, in `ED_ParseField`), its size its scale, its
    picture `frame`, its fading distances `modelindex2` and 3; the client
    passes it to the renderer (`refdef_t.flares`), which adds its glow
    where the z-buffer says its middle can be seen (`R_DrawFlares`,
    `r_fog.c`), and now loads a `.tga` asked for by its own name, grey
    ones too. `target_poi` is the re-release's, teams and stages, and
    its compass an item every player has in single player on its maps
    ("Compass", `Use_Compass`): it finds the way to the point along the
    navigation nodes and shows it a point every 0.2 seconds, the
    re-release's pointer model dropping into place there for ten seconds,
    with its sound to that player alone (an `svc_sound` the game writes
    itself). Its pointers are not the player's alone here, and the point's
    picture on the screen is its HUD's, which this client has not got. `target_light` is a light carried by an
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
    `cl_parse.c`; with the re-release's `skyautorotate`), `target_story` (centre-printed), `target_steam`,
    `func_eye`, The Reckoning's `misc_nuke`, and `monster_makron` in the
    spawn table. `trigger_fog` (`g_trigger.c`) changes a player's fog,
    going in one way or the other, or blended by how far in they are,
    over its `delay`. A brush model's
    `bmodel_anim_` keys are the re-release's texture animation
    (`edict_t.bmodel_anim`, run first in `G_RunEntity` by
    `G_RunBmodelAnimation`): its frame stepped through a range at a
    speed, and through its `alt_` range instead while a button is pressed
    (`button_wait`/`button_done`, no `EF_ANIM01`/`23` then) or after a
    `func_animation` is used. `target_healthbar` puts a boss's health in `STAT_HEALTH_BARS`
    (18; a byte for each of two bars, 0x80 shown and 0 to 127 of it) and
    its name in `CS_HEALTH_BAR_NAME` (the general configstring after the
    shadow lights'), which the single-player status bar draws with the
    client's new `health_bars` layout command (`cl_scrn.c`), as the
    re-release's HUD does; Jorg's goes over to the Makron he throws out
    (`G_HealthBarHandOff`). `target_autosave` runs the engine's new
    `autosave` command (`sv_ccmds.c`): a quiet save to `save0`, the slot
    a level is saved to on being entered, no more than once a minute
    (`g_athena_auto_save_min_time`, as the re-release names it).
    `trigger_flashlight` (in `g_trigger.c`) turns a player's flashlight
    on, off, or by which way they go through it (`P_ToggleFlashlight`,
    `g_items.c`).
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
    The Reckoning's ion ripper (`weapon_boomer`, on cells) and trap
    (`ammo_trap`, thrown as the tesla is, by `Throw_Generic`, which now
    takes the re-release's prime sound and the loop it makes while held)
    are its too, as the re-release has them; the trap eats a monster
    lighter than 400 into a food cube (`item_foodcube`) of a tenth of
    its mass in health, past the most, and whirls its gibs round as it
    chews. The green key too.
    Ground Zero's ETF rifle (flechettes), prox launcher (prox) and A-M
    bomb (`ammo_nuke`, used, as a powerup is) likewise, their
    projectiles in `g_rogue.c` (`fire_prox`, `fire_nuke`,
    `T_RadiusNukeDamage`); `weapon_nailgun` and `ammo_nails`, which the
    re-release spawns as them, are stand-ins for them. The ETF's second
    barrel and the prox have the re-release's own flashes, `MZ_ETF_RIFLE_2`
    (32) and `MZ_PROX` (31): Ground Zero's `MZ_SHOTGUN2` and `MZ_UNUSED`,
    which nothing sent, renumbered as the re-release did, so its demos
    draw them too. The bomb's
    flash is `client->nuke_framenum`, a white blend fading over two
    seconds.
    The spheres and the doppelganger are `g_sphere.c`; a player's one
    sphere is `client->owned_sphere` (saved), which `T_Damage` halves
    their hurt for (the defender) and tells who hurt them (its pain).
    A dead player's hunter, rousing 192 or more away from who killed
    them, takes their view with it (`FL_SAM_RAIMI`, the "sam raimi cam":
    the body gibbed, the view at the sphere, at a field of view of 140,
    looking at its quarry), unless `huntercam` is 0 or dmflags forces a
    respawn, as the re-release has it.
    The IR goggles set `RDF_IRGOGGLES`, which the software renderer
    already draws by; every entity spawned, every gib and every player
    is `RF_IR_VISIBLE`, as the re-release has them.
  - `game/g_rogue.c`, `game/g_ai.c`: Ground Zero's hint paths
    (`hint_path`, `InitHintPaths`, `monsterlost_checkhint`), as the
    re-release has them. `ai_run` looks for one when the monster has not
    seen its enemy for five seconds (and not looked for ten); following
    one is `AI_HINT_PATH`, which `FindTarget` and seeing the enemy end.
    The chains are made anew after a level is loaded, not saved; the
    monster's goal on its chain (`monsterinfo.goal_hint`) is. The
    re-release clears no stale `target_hint_chain` link before building
    its list; here each is.
  - `game/g_rogue.c`: Ground Zero's `trigger_disguise`
    (`FL_DISGUISED`): the sight client passes over a disguised player,
    a monster's `use` and its trigger spawn do not take one for their
    enemy, `PlayerNoise` hears only their shots, and a shot makes them
    `level.disguise_violator` for half a second, whom `FindTarget` takes
    next after a sighting; `FoundTarget` ends the disguise. The player
    wears `RF_USE_DISGUISE`, which the client already draws, and the
    re-release's `i_disguise` health icon where its pak is.
    `target_blacklight` and `target_orb` are the re-release's, its
    `spawngro3` scaled up six and eight times. `info_nav_lock` is there,
    and does nothing: what it
    locks is for the re-release's bots' navigation.
  - `game/g_trigger.c`: `trigger_multiple`'s spawnflag 8 is Ground
    Zero's `TOGGLE` (it starts off; being used turns it on and off,
    instead of firing it), and 16 the re-release's `LATCHED` (it fires
    as the first thing it is for comes into it and as the last leaves,
    looked for each frame), and 32 its `CLIP`: a thing sets it off only
    if its box touches the brush's shape, not just its bounds. That is
    `gi.Clip` (appended to `game_import_t`, `SV_Clip` in
    `server/sv_world.c`: one box trace against one entity's model). The
    same goes for `trigger_push`'s 16 and `trigger_hurt`'s 128, which
    also takes the re-release's `NO_PLAYERS` (32) and `NO_MONSTERS` (64).
    `info_world_text` is in `g_misc.c`: its text is drawn only with the
    re-release's developer tools, so here it is just a point.
  - `game/g_nav.c` (new): the re-release's navigation nodes,
    `bots/navigation/<map>.nav` (versions 2 to 6), and its A* over them.
    The re-release's engine does this, closed; this is the GPL code id
    and Nightdive gave q2pro-ng (`src/server/nav.c`), in the game, with
    a binary heap for its open list. The game reads the file by
    `gi.LoadFile`/`gi.FreeFile`, which this engine's `game_import_t` now
    ends with (a game built for the original engine never reaches
    them). A nav edict's model is the mover's modelindex as it is here
    (`*N` is N+1). What the re-release marks with `SVF_DOOR`,
    `SVF_LOCKED`, `SVF_TRAP` and `SVF_LASER_FIELD` is `FL_DOOR` (the
    three doors), `FL_LOCKED` (a `func_door` a trigger opens),
    `FL_TRAP` (grenades, barrels, a laser on) and `FL_LASER_FIELD`
    here. The nodes are loaded anew with the level, and after a save
    is loaded. `nav_enable 0` leaves them out.
  - `game/m_move.c`: the re-release's `M_MoveToPath` and
    `M_NavPathToGoal` before `M_MoveToGoal`'s own moves, on a map with
    nodes: `AI_PATHING` (its last free bit), the path cached two seconds
    (`monsterinfo.nav_path`), given up after a while blocked
    (`path_blocked_counter`, `path_wait_time`), and the request's node
    search as q2pro-ng's game makes it (within 512, twice its height
    below and above). Its combat styles are worked out: the berserker,
    the mutant and a monster with only a melee close right in; the rest
    to 440. `SV_NewChaseDir` takes a point too. `FacingIdeal` wants
    five degrees on a path; the flight (`g_rr.c`) and the old flying
    step's height go for the path's point; `blocked_checkjump_rr` jumps
    where a traversal is next, facing along it; hint paths and the
    pursuit's next marker are not taken while pathing. Its temporary
    melee rush after being blocked (`AI_TEMP_MELEE_COMBAT`) is not here
    because it never happens there: its `SV_NewChaseDir` counts to three
    blocks, but its one caller outside a path (`M_MoveToGoal`) zeroes the
    count after every call, and the other is on a path, which it rules
    out. A flyer or swimmer that sees its enemy is taken off its path in
    `ai_checkattack` (M_MoveToPath's own rule for them, which an attack
    never reaches, leaving `AI_PATHING` to steer its flight to the old
    point), and is at a node when over it: the nodes are on the floor,
    which it hovers above.
  - `game/g_ai.c`: `ai_run`'s temporary goal is freed with its
    `freetime` cleared, for G_Spawn to have again at once: it is never
    linked or sent, and holding a slot half a second for each monster
    chasing what it can't see ran rbase1 and rware1 out of edicts with
    a hundred of them after the player.
  - `game/g_func.c`: Ground Zero's smart water (`func_water` spawnflag
    2, rmine2's lava): `smart_water_go_up` and `smart_water_blocked`,
    as the re-release has them. Our `func_water` is classnamed
    `func_door`, so smart water is known by its `blocked` instead.
    `p_client.c`'s coop spawns on rmine2 are the lowest
    `info_player_coop_lava` above it.
  - `game/g_xatrix.c`: The Reckoning's `rotating_light`,
    `misc_viper_missile`, `misc_transport`, `misc_amb4` and
    `target_mal_laser`, from the re-release's.
  - `game/p_view.c`, `game/g_items.c`: the re-release's flashlight
    (`item_flashlight`, `FL_FLASHLIGHT`), as `RF_FLASHLIGHT` on the
    player: renderfx goes out as all 32 bits already, so it takes no
    change to the protocol, and a client without it draws nothing.
    `client/cl_ents.c` shines a spotlight (`V_AddSpotLight`) from their
    eyes along their view, a player's other than this one's by their
    model's angles (its pitch is a third of the view's). A `dlight_t`
    with a `spot` is lit by `ref_soft/r_light.c`'s `R_AddSpotLightTo`,
    each lightmap sample by where it is (`R_SampleGrid`), within its
    cone and facing it, and models within it by `R_LightPoint`; through
    walls, as any dynamic light is.
  - `game/g_misc.c`: the re-release's `misc_model` (with the `frame`
    key, which an entity now takes, as there) and
    `misc_player_mannequin`, a player model of a client slot from 253
    up, as its skin configstring, at its "radius" scale (boss2's three
    at .45). The CTF
    banners wave.
  - **CTF** (`game/g_ctf.c`, `g_ctf.h`, `p_menu.c`, `p_menu.h`, new), in
    the base game, as the re-release has it: Threewave CTF from `ctf/`,
    on with `ctf 1` in deathmatch (latched; forced off without
    deathmatch), and its hooks from `ctf/`'s game where they are, each
    kept to CTF -- the flags, techs and grapple (in the item list after
    the rest, so that no item before them moves), its spawn points,
    skins, scores and scoreboard, status bar, team chat (`CheckFlood`,
    now `Cmd_Say_f`'s too), menus, elections, matches, the grapple's
    pull in `ClientThink`, the techs in `T_Damage` and `Weapon_Generic`
    (with The Reckoning's DualFire), `instantweap`. What clashed: its
    stats 17 to 28 are 19 to 30 (after `STAT_SPECTATOR` and
    `STAT_HEALTH_BARS`; its status bar and scoreboard layouts too), its
    match configstring `CS_MAXCLIENTS-1` is `CS_AIRACCEL` and so is
    after the health bar's name, and its dmflags 131072 to 524288 are
    Ground Zero's, and are the re-release's cvars instead:
    `g_teamplay_force_join`, `g_teamplay_armor_protect`, `g_allow_techs`
    and `g_allow_grapple` ("auto": in CTF). Its own teleporters and
    banners give way to the base game's. `ctf/`'s library is still built.
  - **Tag** (`game/g_newdm.c`, new), Ground Zero's deathmatch game,
    `gamerules 1`, as the re-release has it: `DMGame`, the hooks a game
    sets (each level anew, `InitGameRules` in `SpawnEntities`), called in
    `T_Damage`, `player_die`, `ClientDisconnect`, `G_SetClientEffects`,
    the scoreboard's dogtags, and the obituary's scores (`G_Score`); the
    token is `dm_tag_token` (put at a spawn point if the map has none),
    the item "Tag Token". Its deathball, `gamerules 2`, is not here.
  - **Scale**, the re-release's (`entity_state_t.scale` there):
    `entity_state_t` begins the game's `edict_t`, so it cannot grow
    without breaking the mission packs' game libraries built from
    `rogue/` and `xatrix/`. The game says it by `gi.SetEntityScale`, at
    the end of `game_import_t` (`game/g_utils.c`'s `G_SetScale`, which
    `G_InitEdict` and `G_FreeEdict` clear and `ReadLevel` says again);
    the server keeps it beside the states (`sv.entscale`,
    `svs.client_entity_scales`) and sends a change as `U_SCALE` (bit 28,
    a short of 256ths) by `MSG_WriteDeltaEntityScale`, only to a client
    whose userinfo has `q2ext`, which this one's does; a client without
    it sees the model as it is. The client keeps it beside
    `cl_parse_entities` (`cl_parse_scales`) and gives it the refresh as
    `entity_t.scale`, which `ref_soft/r_alias.c` scales the model's axes
    by, about its origin, and the lerp's move back down by. The game:
    any entity's "scale" key, as the re-release's; a monster's box,
    mass and stride scaled with it in `monster_start`; its gibs as big;
    the gunner commander at 1.25, the N64 tank commander and
    `monster_tank_stand` at 1.5, the trap's food cube by what it ate.
    Ground Zero's spawn-in effect keeps its own models rather than
    scaling one.
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
  over is the re-release's decoration (`misc_model`,
  `misc_player_mannequin`).
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
  (`GIB_ACID`: the green trail, full bright); the stalker
  (`m_stalker_rr.c`, on Ground Zero's frames and its ceiling walking),
  told by `monsterinfo.physics_change` (saved) as it gains or loses its
  ground, which the step physics now calls as the re-release's does, and
  marked to circle as it fires (`AI_CIRCLE_STRAFE`), as this game's
  `ai_charge` circles only those it marks; and the turret
  (`m_turret_rr.c`, on Ground Zero's frames), which wears combat armor,
  as only it does there: `monsterinfo.armor_type` (an item index) and
  `armor_power`, which `CheckArmor` takes from for a monster as from a
  player's inventory, till it is spent; and the carrier
  (`m_carrier_rr.c`), on the re-release's flight, its checkattack the
  re-release's `M_CheckAttack_Base` (`g_rr.c`: chances by range given,
  a flyer's strafing kept a while, by `monsterinfo.strafe_check_time`),
  its summoned flyers set going on `m_flyer_rr.c`'s own moves
  (`flyer_move_attack3_rr`, `flyer_move_kamikaze_rr`). `M_MoveFrame`
  stops once an end function has made the monster its head
  (`ThrowGibs`), not to set the gib to its move's first frame.
  The fixbot (`m_fixbot.c`, its frames' header made from the
  re-release's enum) is The Reckoning's, which this game had none of,
  so it is the re-release's on every map, with `func_object_repair`
  (`object_repair`) for it to weld. Its healing ray is the lasting
  laser's (`dabeam_update_rr`) with a negative `dmg`, which heals what
  it touches short of full health; the medic's `abortHeal` is shared
  with it. Its Fixit spawnflag (4) is spared `monster_start`'s 1997
  rewrite of 4 into Ambush, and a trigger-spawned one bent on landing,
  taking off or fixing skips its first target, as the re-release's
  does. As there, it keeps `AI_MEDIC` after a raise.
  The black widow (`m_widow.c`) and its second form (`m_widow2.c`,
  with its first form's legs and its own gibs), Ground Zero's final
  boss, likewise the re-release's on every map, their frames' headers
  made from its enums. Their monster powerups (`monsterinfo.quad_time`,
  `double_time`, `invincible_time`, in seconds; a player's frames
  times `FRAMETIME`) show as shells in `M_SetEffects`, blinking out as
  the re-release's do; their damage is the widow's multiplier. Its
  helpers are in `g_rr.c`: `G_ProjectSource2`, `CountPlayers`,
  `monster_fire_heatbeam` and `monster_fire_tracker`. `monster_think`
  stops if a frame has freed its monster (the widow, become its legs).
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
  Some of those steps (carpet, grass, snow, splash, tile), and
  `world/klaxon3.wav`, are 24-bit PCM; `ResampleSfx`
  (`client/snd_mem.c`) takes the top 16 bits of a 24- or 32-bit sample,
  which it read before as unsigned 8-bit, hence a screech.

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

**`ref_soft/r_rast.c`, `client/cl_view.c`**: the sky turns. id's software
renderer ignored `skyrotate`, which only its OpenGL one used. The client
works out how far it is turned each frame (`refdef_t.skyangle`: the time
by `CS_SKYROTATE`'s speed, or, the re-release's second number 0 --
`skyautorotate` -- that many degrees and still, as the re-release's base
maps turn theirs so that their suns are where their light comes from),
and `R_EmitSkyBox` turns the box's corners, planes and texture axes about
`skyaxis` by it.

**`ref_soft/r_fog.c`** (new), **`client/cl_parse.c`**, **`server/sv_game.c`**:
the re-release's fog, its distance fog and its height fog, which 102 of its
maps have and trigger_fog changes as you go. The game works out the fog each
player is to have (the world's fog keys, then `trigger_fog`'s) and says it
by `gi.SetFog` (appended to `game_import_t`) when it changes, with how long
to take over it; the server sends it, reliably, as `svc_fog` (after
`svc_muzzleflash3`), to clients that say `q2ext`; the client goes over to
it in that time and gives it the renderer in `refdef_t.fog`. The renderer
goes over the view once drawn, before the underwater warp, by the depths
the z-buffer kept (0x8000/z; the sky's are negative), with the formulas of
the re-release's shaders as q2pro-ng has them: distance fog by depth, the
sky by its sky factor, height fog by where each pixel is in the world. The
colours are mixed and taken back to the palette through sw_colorlight's
nearest colours; distance fog alone through tables of 64 levels for each
colour, dithered, height fog for each pixel (its extinction and colour for
each 2x2 block). `sw_fog 0` turns it off.

**`ref_soft/r_md5.c`** (new), **`ref_soft/r_model.c`**, **`ref_soft/r_alias.c`**:
the re-release's md5 models. Next to 198 of its `.md2` models it has an
`md5/` directory with the same model in more polygons: a Doom 3 `.md5mesh`,
an `.md5anim` whose frames are the `.md2`'s frame for frame, sometimes an
`.md5scale` (JSON: joints shrunk on some frames, gibbed limbs mostly), and
its skins as `.png` with more pixels (up to 512x1024). Software draws no
skeletons, so `R_LoadMD5` poses and skins each frame once, at load, as
q2pro-ng's `models.c` does, and puts the lot back together as an `.md2`
that `Mod_LoadAliasModel` takes as it takes any other: one vertex for each
of the mesh's, each frame's positions quantized to a byte (scale and
translate for each frame, as an `.md2`'s), its normals the nearest of the
162 the lighting has (faces' normals, summed over the vertices a seam
splits), its texture coordinates in the first md5 skin's pixels, its skins
the `md5/` ones where there are. Frames the md5 has not are its last. An
entity's own skin (a player's) is used in its `md5/` version if there is
one (`R_MD5Skin`), and the texture coordinates are scaled to the skin
drawn if it is another size (`R_AliasPreparePoints`). For them
`MAX_LBM_HEIGHT` is 1024 and the alias vertex limit 4096 (the biggest has
3089), its work array static. It costs about 13 ms a model at load.
`sw_md5 0` and a `vid_restart` go back to the `.md2`s.

**`client/cl_wheel.c`** (new), **`client/cl_input.c`**, **`game/g_items.c`**:
the re-release's weapon and powerup wheels, which its `default.cfg` binds
(`+wheel` on e, `+wheel2` on q, `cl_weapnext`/`cl_weapprev` on the mouse
wheel) and which were unknown commands here. The re-release's game tells its
client what a player has by stats protocol 34 has no room for, so here the
game names what goes on each wheel, in the re-release's order, once a level
(`CS_WHEEL_WEAPONS`: item, ammo item, least ammo, icon; `CS_WHEEL_POWERUPS`:
item, icon; after `CS_CD_LOOP_COUNT`), and the client asks what the player
has when one is held and every half second after (`wheelinv`, the
inventory as `inven` sends it, without showing it). The client only asks a
game that named them: the mission packs' games would say an unknown
command as chat. Held, `CL_CreateCmd` puts back the angles the mouse or the
look stick turned and moves the wheel's cursor by them instead (freelook on
meanwhile, for the mouse's up and down), and the trigger is kept off; let
go, what it is on is `use`d. Its pictures are the re-release's 24-pixel
`pics/wheel/` ones, lit when chosen, half as big again at 480 lines, so
`Draw_StretchPic` now leaves a picture's transparent pixels as they are;
the inventory's icons without them. Held in single player (this
process's own server, `maxclients` 1, not paused) the game goes slower, as
the re-release's engine slows it (`SERVER_FLAG_SLOW_TIME`; how much is the
engine's, unpublished, so `cl_wheel_timescale`, 0.25): `Qcommon_Frame`
scales the milliseconds both the server and the client are given, keeping
what is left of one for the next frame, and `CL_Frame`'s `cl_maxfps` and the
look stick's speed are reckoned in real time so that the screen and the
cursor do not slow with it.

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
