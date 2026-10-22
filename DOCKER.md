# Running Quake II in Docker

The image runs the engine on a private X server and serves it to a browser
with noVNC; the sound goes to the same page over the same port. You bring the
game data.

```
docker run --rm -p 6080:6080 \
  -v /path/to/quake2:/quake2data:ro \
  -v quake2-state:/quake2/state \
  ghcr.io/krippler/quake-2
```

Then open <http://localhost:6080/play.html> (the bare address redirects there).

Arguments after the image name go to the engine:

```
docker run ... ghcr.io/krippler/quake-2 +map base1 +skill 2
```

## Game data

Mount the directory holding `baseq2/pak0.pak`, read-only. Nothing is written
there: the container builds a game directory of its own in the state volume
(`/quake2/state/game`) and links your files into it, because Quake II writes
`config.cfg`, its savegames and screenshots into the game directory, beside
the pak files.

The container finds `baseq2` itself when the mount is a level or more above
it, so a whole Steam or GOG install directory works. The 2023 re-release
keeps its own data in a `rerelease` folder beside the original game's
`baseq2`, which is what plays by default; see below for playing its pak too.

### The 2023 re-release's pak

`-e QUAKE2_RERELEASE=1` puts the re-release's pak
(`rerelease/baseq2/pak0.pak`, mounted with the rest of the install) on top of
the original game's. For every file the engine then takes the first copy it
can read, so you get the re-release's maps -- with their new lighting --
models and textures, and wherever the re-release's copy is missing or in a
form a 1997 engine cannot use, the original's stands in. Its console font,
for one, is not in the re-release's pak at all; the original's is used, and
the menus and status bar look as they always did.

What plays from it, by the re-release's own maps:

| | |
| --- | --- |
| the base campaign, The Reckoning, the deathmatch maps | yes, from the re-release's copies |
| `q2dm3` and four Ground Zero maps | from the original game's copies: the re-release's have faces wider than the software renderer draws |
| the N64 campaign (`q64/`) | no: wider faces, and textures in a format of its own |
| Call of the Machine (`mgu*`) | no: 12 of its maps are in the re-release's extended QBSP format, and the rest have more texture entries than a 1997 engine loads |
| its demos | yes: recorded in its own network protocol, and translated to 1997's as they play |

Each copy passed over is named in the log with the reason. The re-release's
maps name their messages -- the help computer's objectives, "You found a
secret area!" -- and keep the words in `Q2Game.kpf` beside its `baseq2`;
the container takes them out of it and the game shows them in the original
font. Mount the re-release's whole folder, or copy `Q2Game.kpf` anywhere
into the mount; without it the log says so, and each message is worded from
its name instead ("Establish communication", "Secondary mission
objective"), which is close but not always all of it. The re-release's
see-through grates are not drawn as such.

Each other directory with pak files, a `maps` directory or a
`gamex86_64.so` in it is a game: `ctf`, a mission pack, a mod. Start on one
with `-e QUAKE2_GAME=xatrix`; New Game in the menu then starts that game's
campaign. From the console, `game rogue` followed by a `map` switches too.

| What you have | What happens |
| --- | --- |
| `baseq2/pak0.pak` to `pak2.pak` | the full game; New Game starts at base1 |
| `baseq2/pak0.pak` only | id's demo, whose maps are `demo1` to `demo3`. The log says so |
| `ctf/` | Threewave CTF, with its own game code from this image |
| a mod with a `gamex86_64.so` | that mod's code |
| a mod without one | the single-player game's code, with the mod's data |
| `xatrix/` | The Reckoning, with its own game code from this image |
| `rogue/` | Ground Zero, with its own game code from this image |

The container's log lists what it found every time it starts the engine, and
it looks again before each restart, so a mod copied in while it is running is
there after the next quit.

### Music

The soundtrack was audio tracks 2 to 11 on the CD. A rip as `track02.ogg`,
`track03.ogg` and so on (or `.flac`, `.opus`, `.mp3`, `.wav`, any case) in
`baseq2/music` plays when a map asks for it. Each game's own `music`
directory wins, then a `music` directory at the top of the mount (where the
GOG release keeps it), then `baseq2/music`. `cd info` at the console says
which directory it is using. **Options → music volume** sets its level
apart from the sound effects'.

## Settings

| Variable | Default | |
| --- | --- | --- |
| `QUAKE2_WIDTH`, `QUAKE2_HEIGHT` | 640, 480 | The resolution a fresh state volume starts at, up to 1920x1200. After that the Video menu's choice is in `config.cfg` and wins |
| `QUAKE2_GAME` | | The game directory to start on: `xatrix` (The Reckoning), `rogue` (Ground Zero), `ctf`, or a mod. Empty is the base game |
| `QUAKE2_RERELEASE` | 0 | 1 plays the 2023 re-release's pak on top of the original game's ([above](#the-2023-re-releases-pak)) |
| `QUAKE2_MODERN_CONTROLS` | 1 | 0 starts a fresh volume on id's 1997 controls instead (below) |
| `QUAKE2_VNC_PASSWORD` | | A password for the picture. Without it there is none |
| `PUID`, `PGID` | 1001 | Who the container runs as, and so who owns the state volume's files. 99 and 100 on Unraid |
| `QUAKE2_SOUND` | 1 | 0 turns the sound off |
| `QUAKE2_SND_MIXAHEAD` | 0.06 | Seconds the engine mixes ahead. Raise towards id's 0.2 if the sound breaks up on a slow machine |
| `QUAKE2_RESTART` | 1 | Bring the engine back when it quits or crashes. 0 stops the container instead |
| `QUAKE2_X_DEPTH` | 24 | 8 runs the X server at 8 bits with a colormap, as id's renderer was written for. 24 is faster end to end |
| `QUAKE2_MAX_WIDTH`, `QUAKE2_MAX_HEIGHT` | 1920, 1200 | The largest mode the Video menu can reach |
| `QUAKE2_VNC_ARGS` | | Extra options for x11vnc |
| `QUAKE2_WEB_PORT` | 6080 | The port inside the container |

## Ports

`6080` is everything the browser needs: the page, the picture, the sound and
the controller, over one WebSocket port. `5900` is raw VNC for a native client,
picture only; publish it with `-p 5900:5900` if you want it.

There is no TLS. Put a reverse proxy in front of it rather than exposing it to
the internet.

## Controls

The page captures the mouse when you click Play. `Esc` gives it back and shows
the start screen again; that is the browser's, and it never reaches the game.
So:

- **Backspace** is the menu key. From the game it opens the menu (or puts the
  help computer away); in the menus it goes back a level, except in a text
  field, where it deletes. Like `Esc`, it cannot be bound to anything else.
- The **Game menu** button on the start screen opens the menu too.
- `` ` `` opens the console; `F1` the help computer, with the objectives.

A fresh state volume starts with these, on top of id's `default.cfg`:

| | |
| --- | --- |
| `W` `A` `S` `D` | move |
| mouse | look (`freelook 1`) |
| left / right button | fire |
| wheel | next / previous weapon |
| `Space` / `Ctrl` | jump / crouch |
| `Shift` | walk (`cl_run 1` is always run) |
| `E` / `F` | use the selected item / inventory |

`QUAKE2_MODERN_CONTROLS=0` leaves them out: the arrow keys move, `Ctrl` fires,
and the mouse looks only while `\` is held. Either way the game rewrites
`config.cfg` when it exits, so what you change in **Options → Customize
Controls** is what stays. Delete `/quake2/state/game/baseq2/config.cfg` to
start over.

### Game controllers

Any pad the browser sees as a standard gamepad: an Xbox or PlayStation pad, a
Backbone, on a desktop or a phone. The page reads it and hands its state to
the engine, which treats its buttons as keys (`PAD_A`, `PAD_B`, ... `PAD_START`)
bound like any other in Customize Controls. The first time a pad turns up
these are bound:

| | |
| --- | --- |
| left stick / right stick | move / look |
| RT / LT | fire / walk |
| A / B | jump / crouch |
| X / Y | use item / inventory |
| RB / LB | next / previous weapon |
| right / left stick click | next / previous item |
| Back | the help computer |
| Start | the menu |

In the menus A chooses, B goes back and the left stick or d-pad moves.
`joy_lookspeed`, `joy_lookspeed_y`, `joy_invert`, `joy_deadzone` and
`joy_swapsticks` at the console tune the sticks.

## Video

**Options → Video** lists every mode from 320x240 to 1920x1200, widescreen
included; the container's X screen is resized to match, and the page scales
the picture to the window. Widescreen modes see more to the sides rather than
less at the top and bottom.

The menus, the console and the status bar are drawn larger as the resolution
goes up, at a whole multiple of their size (three times at 1920x1200), so
they stay about the size they were at 640x480. `scr_scale` at the console
sets the multiple yourself; `scr_scale 1` is id's, pixel for pixel.

The maps' coloured light -- the orange of fires, the red of alarms, the
blue and green of the re-release's -- is drawn in the palette's colours, as
are coloured flashes from weapons. **coloured light** in Options → Video
turns it off, as in 1997, or to half; `sw_colorlight` at the console is the
same setting.

The re-release lights its remade maps with real-time lights at their lamps
as well as their lightmaps, some of them spotlights, some flickering, some
switched by the level. They are drawn here as white light on the walls,
floors and monsters each lamp can see -- not through walls, but without the
re-release's shadows of moving things. **lamp lights** in Options → Video
turns them off (`cl_shadowlights 0`); `cl_shadowlight_scale`, 80 by
default, is how bright they are. A lit surface is kept in the renderer's
cache while its lamps stay as they are, so they cost next to nothing.

The renderer is in software, so every pixel costs CPU. The log says how many
cores the container was given.

## Compose

[docker-compose.yml](docker-compose.yml) builds the image and mounts
`./quake2data`:

```
mkdir -p quake2data && cp -r /path/to/quake2/baseq2 quake2data/
docker compose up --build
```

## Unraid

[templates/unraid.xml](templates/unraid.xml) is a Community Applications
template. Point **Game data** at your Quake II directory; `PUID`/`PGID` are
99/100 there.

## Troubleshooting

**"No Quake II game data found"** -- the mount does not have `baseq2/pak0.pak`
within four levels of its top. Check the path on the left of `-v`.

**"pics/conchars.pcx is missing"** -- the pak files are the 2023
re-release's alone (its `rerelease/baseq2/pak0.pak`, 14,000-odd files). Its
pak has no console font and no 1997 pictures, and plays only on top of the
original game's: mount the whole install, with `QUAKE2_RERELEASE=1` if you
want the re-release's pak as well.

**"cannot write to /quake2/state"** -- a host directory was bind-mounted there
and the container's user cannot write to it. Use `PUID`/`PGID`, `--user`, or a
named volume.

**No sound** -- the start screen says what the page is doing with it
("Sound: on — ..."), and the container's log has a `sound:` line from the page.
A browser plays sound only after a click on the page, which Play is.

**The picture freezes or stutters** -- `?stats=1` on the page's address puts a
line on the start screen saying whether the picture or the link went quiet,
and the log has `picture gap` lines from the proxy.

**The engine crashed** -- the log has the signal and a backtrace; the
container brings the engine back on its own. Three crashes within seconds of
starting stop it instead, with the reason above.

**Logs** -- the container's own log is the one to read. Xvfb, x11vnc,
websockify and audiostream each have a log in the state volume as well.
