# Building it yourself

## The image

```
docker build -t quake2 .
```

Or with Compose, which builds the image and mounts `./quake2data` as the game
data (see [DOCKER.md](DOCKER.md#compose)):

```
mkdir -p quake2data && cp -r /path/to/quake2/baseq2 quake2data/
docker compose up --build
```

## Without a container

The engine needs an X display at depth 8, 15, 16 or 24, plus the X11
(`libx11`, `libxext`, `libxrandr`) and libsndfile development packages:

```
make -C linux               # -> linux/build/
make -C audiostream         # -> audiostream/linux/audiostream
```

What comes out sits together and is found by the engine beside itself:

| | |
| --- | --- |
| `linux/build/quake2` | the engine |
| `linux/build/ref_softx.so` | the software renderer, drawing to X11 |
| `linux/build/baseq2/gamex86_64.so` | the single-player game |
| `linux/build/ctf/gamex86_64.so` | Threewave CTF |
| `linux/build/xatrix/gamex86_64.so` | The Reckoning, from `xatrix/` |
| `linux/build/rogue/gamex86_64.so` | Ground Zero, from `rogue/` |

```
linux/build/quake2 +set basedir /path/to/quake2
```

`make -C linux` takes `SOUND=stream|alsa|oss` and `MUSIC=sndfile|cd|none`;
the defaults are what the image uses. `DEBUG=1` builds without optimisation
and with symbols, and `B=dir` puts the build somewhere other than
`linux/build`, which is how two configurations live side by side.

`xatrix/` and `rogue/` are Yamagi Quake II's mission packs, as upstream has
them but for two changes the 2023 re-release's maps need (PORTING-NOTES.md,
"The mission packs"); `UPSTREAM` in each names the repository and commit
and lists the changes. To update one, replace its `src/`, `LICENSE`,
`README.md` and `CHANGELOG` with a newer upstream checkout's, change
`UPSTREAM` to match, and carry the changes across.

The top-level `makefile` is id's, for NeXTSTEP/Rhapsody, and is left as it
was. So are `linux/Makefile.i386` and `Makefile.AXP`, Zoid's originals, and
the Windows, IRIX and Solaris trees.

## For a Linux desktop

`SOUND=alsa` is the build for playing on the machine itself. Its sound goes to
ALSA's default device, which is PipeWire or PulseAudio where either is
running. It needs the ALSA development package as well:

```
make -C linux SOUND=alsa
sudo make -C linux SOUND=alsa install          # into /usr/local
make -C linux SOUND=alsa install PREFIX=~/.local
```

That installs the `quake2` launcher (`desktop/quake2`), the engine and its
libraries in `lib/quake2`, and a menu entry. `tools/build-linux-tarball.sh
VERSION` builds the release tarball the same way, with `install.sh` in it.
CI builds one on every push, on Ubuntu 22.04, so that it runs on
distributions that old.

## The smoke test

```
make -C linux && tools/smoke-test.sh
```

There is no game data in the repository to test against, so
`tools/make-test-data.py` builds its own: a palette and colormap, a console
font, a few tones, and a one-room map written as an IBSP 38 file by hand. The
test then starts the engine on a throwaway Xvfb and checks that it

1. draws the console, resizes the X screen to the mode it is in, changes mode
   at runtime and back, quits cleanly on SIGTERM writing `config.cfg`, and
   puts a 440 Hz tone on its audio pipe at the right pitch;
2. loads the map through each game library -- the single-player game and
   both mission packs -- draws it, and saves and loads a game in each, which
   is each library's 64-bit pointer handling, round trip;
3. still gets sound to the pipe after a three-second stall;
4. plays the game's own music directory in preference to `$QUAKE2_MUSICDIR`;
5. draws at depth 8, through a colormap;
6. with a stand-in for the 2023 re-release's pak on top, passes over the
   copies of its map and picture it cannot read for the original's, loads a
   map only that pak has, plays a demo in the re-release's protocol as far
   as a print it carries, and passes over a demo in no protocol it knows.

Point it at real data and it also loads the demo's or the full game's first
map:

```
QUAKE2_SMOKE_DATA=/path/to/quake2 tools/smoke-test.sh
```

It needs Xvfb, xdotool, xdpyinfo, xwd and ImageMagick. CI runs it on every
push, along with builds of the other `SOUND` and `MUSIC` combinations.
