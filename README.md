# Quake II

id Software's 2001 GPL source release of Quake II, repaired to build and run
on current 64-bit Linux and packaged to play in a browser: picture and sound
both arrive there, and nothing is installed on the host. There is also a build
to install and play [on a Linux desktop](#on-a-linux-desktop), without Docker.

This is the same treatment [Krippler/Quake](https://github.com/Krippler/Quake)
gave the first game, and the container, the browser client and the desktop
launcher work the same way.

## Quick start

```
docker run --rm -p 6080:6080 \
  -v /path/to/quake2:/quake2data:ro \
  -v quake2-state:/quake2/state \
  ghcr.io/krippler/quake-2
```

Open **<http://localhost:6080/play.html>** and click to play.

- `/path/to/quake2` is your own Quake II install: the directory holding
  `baseq2/pak0.pak`.
- `quake2-state` keeps your settings and savegames between runs. Leave it out
  and they go when the container does.

In the browser `Esc` releases the mouse, so **Backspace** is the menu key, in
the game and in the menus. `F1` shows the mission's objectives and `` ` ``
opens the console.

## On a Linux desktop

Each [release](https://github.com/Krippler/Quake-2/releases) has a
`quake2-linux-x86_64-VERSION.tar.gz`: the same engine, playing in a window of
its own, with its sound going to PipeWire or PulseAudio. It runs on Ubuntu
22.04, Debian 12, Fedora 35 or anything newer, under X11 or XWayland.

```
tar xzf quake2-linux-x86_64-*.tar.gz
cd quake2-linux-x86_64-*/
./install.sh          # into ~/.local; sudo ./install.sh /usr/local for everyone
quake2                # or Quake II in the desktop's menu
```

A game controller works in both, through the game's own key bindings.

It finds a Steam or GOG copy by itself. Otherwise run
`quake2 --data /path/to/quake2` once, and it will remember. Your game files
are only read. Settings and saves go in `~/.local/share/quake2`. The tarball's
README.txt has the rest.

## Game data

None is included. Quake II's data is id Software's and is not free to
redistribute. Any of these works:

- a Steam or GOG install, or a CD install: the directory with `baseq2/` in it.
  A Steam install of the 2023 re-release still has the original game's
  `baseq2` beside its `rerelease` folder, and that is the one this engine
  plays; mounting the whole install directory finds it.
- id's free demo (`q2-314-demo-x86.exe`, a zip archive): its
  `Install/Data/baseq2` holds `pak0.pak` and the three demo maps.

```
/path/to/quake2/
├── baseq2/pak0.pak, pak1.pak, pak2.pak
├── baseq2/music/track02.ogg ...   the soundtrack, if you have a rip
├── ctf/pak0.pak                   Threewave CTF
├── xatrix/pak0.pak                The Reckoning
├── rogue/pak0.pak                 Ground Zero
└── <mod>/                         anything else
```

**Music** is not in the pak files; it was audio tracks on the CD. Rip it to
`baseq2/music/track02.ogg`, `track03.ogg` and so on, and it plays. The GOG
release's `music/` folder is found by itself.

**Mission packs.** The Reckoning (`xatrix`) and Ground Zero (`rogue`) play
with their own game code, which this repository carries in Yamagi Quake II's
GPLv2 versions ([`xatrix/`](xatrix), [`rogue/`](rogue)). Put each pack's
directory beside `baseq2` and start on it with `-e QUAKE2_GAME=xatrix` (or
`rogue`); New Game then starts the pack's own campaign. A mod that ships a
`gamex86_64.so` built for this engine or for Yamagi Quake II (the game API is
the same) runs that; anything else runs on the single-player game's code,
which is fine for data-only mods.

## Common settings

| | |
| --- | --- |
| `-e QUAKE2_WIDTH=1280 -e QUAKE2_HEIGHT=720` | starting resolution; later, **Options → Video** |
| `-e QUAKE2_GAME=xatrix` | the game to start on: `xatrix`, `rogue`, `ctf` or a mod |
| `-e PUID=1000 -e PGID=1000` | who owns the saved files (default 1001) |

Everything else, including controls, game controllers, sound, Compose and
Unraid, is in [DOCKER.md](DOCKER.md).

## Documentation

| | |
| --- | --- |
| [DOCKER.md](DOCKER.md) | Running it: game data in detail, every setting, controls, troubleshooting |
| [BUILDING.md](BUILDING.md) | Building the image or the engine yourself, and the smoke test |
| [ABOUT.md](ABOUT.md) | What the 2001 sources needed and what the port added |
| [PORTING-NOTES.md](PORTING-NOTES.md) | Every change made to the sources, and why |
| [PUBLISHING.md](PUBLISHING.md) | Releases, image tags, and the Unraid listing |
| [CHANGELOG.md](CHANGELOG.md) | What changed in each release |
| [readme.txt](readme.txt) | id Software's original 2001 release note |

## Licence

The sources are GPLv2 ([gnu.txt](gnu.txt)), as id released them in December
2001. That does not cover the game data, which comes from your own copy and is
never in this repository or the images.

QUAKE II is a trademark of id Software LLC. This is an unaffiliated port of
the sources they published.
