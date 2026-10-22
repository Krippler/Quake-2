#!/bin/sh
#
# Start the engine against synthetic data, on a real X server, and check that
# it draws, plays a map, saves and loads a game, and produces sound.
#
# There is no game data in this repository and there cannot be, so this makes
# its own: tools/make-test-data.py writes a baseq2/pak0.pak holding the
# palette and colormap, the console font, the status bar's pictures, a few
# tones and a one-room map. None of it is id's.
#
# The phases:
#
#   1  the console: the pak reader, the palette, the 2D drawing, the X11 path
#      at depth 24, the screen resize RANDR does, a video mode change at
#      runtime, a clean shutdown on SIGTERM that writes config.cfg, and a
#      440 Hz tone through the mixer and the resampler to the audio pipe.
#   2  the map: the game library loads, a server spawns, a client connects,
#      the room is drawn through the 3D renderer, and a game is saved and
#      loaded again -- which is the game library's own 64-bit pointer
#      juggling (g_save.c) round trip. Then the same under each mission
#      pack's library.
#   3  a three-second stall, after which the mixer must still reach the pipe.
#   4  which music directory the engine picks.
#   5  depth 8: the renderer's PseudoColor path, with its colormap.
#   6  a stand-in for the 2023 re-release's pak on top: what of it this
#      engine cannot read gives way to the original's, a sky and a texture
#      it has only as 24-bit TGAs are drawn from them, and a demo in its
#      protocol plays.
#
# Given real game data it goes further: point QUAKE2_SMOKE_DATA at a directory
# holding baseq2/pak0.pak and a last phase loads the demo's first map.
#
#   tools/smoke-test.sh [workdir]
#   QUAKE2_SMOKE_DATA=/path/to/quake2 tools/smoke-test.sh
#
# Needs Xvfb, xdotool, xdpyinfo, xwd and ImageMagick's convert.
#
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
work="${1:-${TMPDIR:-/tmp}/quake2-smoke}"
disp="${QUAKE2_SMOKE_DISPLAY:-:97}"
build="${QUAKE2_SMOKE_BUILD:-$here/linux/build}"
engine="$build/quake2"

say() { printf '[smoke] %s\n' "$*"; }
die() { printf '[smoke] FAILED: %s\n' "$*" >&2; exit 1; }

[ -x "$engine" ] || die "no engine at $engine -- run: make -C linux"
for tool in Xvfb xdotool xdpyinfo xwd convert python3; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool is not installed"
done

rm -rf "$work"
mkdir -p "$work"
cd "$work"

say "building synthetic game data"
python3 "$here/tools/make-test-data.py" "$work" >/dev/null

xvfb_pid=""
game_pid=""
cat_pid=""

cleanup() {
    [ -n "$game_pid" ] && kill -KILL "$game_pid" 2>/dev/null || true
    [ -n "$cat_pid" ] && kill "$cat_pid" 2>/dev/null || true
    [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Deliberately larger than the mode the engine is asked for, so that the
# screen resize vid_resizescreen does is something this test can see happen.
# -noreset, as the entrypoint passes: without it Xvfb resets when the last
# client disconnects, and the next phase lands in the middle of that.
start_x() {
    [ -n "$xvfb_pid" ] && { kill "$xvfb_pid" 2>/dev/null || true; wait "$xvfb_pid" 2>/dev/null || true; }
    rm -f "/tmp/.X11-unix/X${disp#:}" "/tmp/.X${disp#:}-lock"
    say "starting Xvfb on $disp at 800x600x$1"
    Xvfb "$disp" -screen 0 "800x600x$1" -nolisten tcp -noreset >"$work/xvfb-$1.log" 2>&1 &
    xvfb_pid=$!
    i=0
    while [ ! -e "/tmp/.X11-unix/X${disp#:}" ]; do
        i=$((i + 1))
        [ "$i" -gt 100 ] && die "Xvfb did not start; see $work/xvfb-$1.log"
        sleep 0.1
    done
}

# The engine, in the background, with its sound going to a file through a
# FIFO as it would to audiostream. $1 names the run; the rest are arguments.
run_engine() {
    name="$1"
    shift
    rm -f "$work/$name.fifo" "$work/$name.raw"
    mkfifo "$work/$name.fifo"
    cat "$work/$name.fifo" > "$work/$name.raw" &
    cat_pid=$!
    env DISPLAY="$disp" QUAKE2_AUDIO_FIFO="$work/$name.fifo" ${RUN_ENV:-} \
        "$engine" +set basedir "$work" +set vid_resizescreen 1 "$@" \
        >"$work/$name.log" 2>&1 &
    game_pid=$!
}

stop_engine() {
    kill -TERM "$game_pid" 2>/dev/null || true
    i=0
    while kill -0 "$game_pid" 2>/dev/null && [ "$i" -lt 50 ]; do
        i=$((i + 1))
        sleep 0.1
    done
    kill -0 "$game_pid" 2>/dev/null && die "the engine did not stop on SIGTERM"
    wait "$game_pid" 2>/dev/null && status=0 || status=$?
    game_pid=""
    kill "$cat_pid" 2>/dev/null || true
    cat_pid=""
    sleep 0.5
}

alive() {
    kill -0 "$game_pid" 2>/dev/null || die "the engine exited; see $work/$1.log"
}

# Waited for rather than slept on: how long the engine takes is up to whatever
# else the machine is doing, and a fixed sleep is a test that passes on a
# laptop and flakes on a shared runner.
wait_for_log() {
    i=0
    while [ "$i" -lt 150 ]; do
        grep -q "$2" "$work/$1.log" 2>/dev/null && return 0
        kill -0 "$game_pid" 2>/dev/null || return 1
        i=$((i + 1))
        sleep 0.2
    done
    return 1
}

# xdpyinfo opens a fresh connection each time, which is the point:
# DisplayWidth/DisplayHeight are cached per connection and do not follow a
# RANDR resize.
screen_size() {
    DISPLAY="$disp" xdpyinfo 2>/dev/null \
        | sed -n 's/^  dimensions: *\([0-9]*x[0-9]*\) .*/\1/p' | head -1
}

wait_for_screen() {
    i=0
    while [ "$i" -lt 100 ]; do
        [ "$(screen_size)" = "$1" ] && return 0
        i=$((i + 1))
        sleep 0.2
    done
    return 1
}

# Commands typed at the console. XTEST delivers to whatever has the input
# focus, and with no window manager that is the window under the pointer.
console() {
    DISPLAY="$disp" xdotool mousemove 100 100
    DISPLAY="$disp" xdotool key grave
    sleep 0.5
    console_type "$1"
    DISPLAY="$disp" xdotool key grave
}

# A command at a console that is already open, left open: during a demo any
# key is taken for Escape, which ends it, so the console is not closed
# behind a demomap.
console_type() {
    DISPLAY="$disp" xdotool type --delay 25 "$1"
    DISPLAY="$disp" xdotool key Return
    sleep 0.3
}

colours() {
    DISPLAY="$disp" xwd -name quake2 > "$work/$1.xwd" 2>/dev/null \
        || die "could not capture the window -- is it there at all?"
    convert "$work/$1.xwd" "$work/$1.png"
    convert "$work/$1.png" -format %k info:
}

# The pitch of a stretch of the left channel, by zero crossings; then its
# peak, the length of the whole recording and how much of the stretch sounds.
tone() {
    python3 - "$@" <<'PY'
import struct, sys

raw, start, length = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
RATE = 22050
data = open(raw, 'rb').read()
frames = len(data) // 4
left = struct.unpack('<%dh' % (frames * 2), data[:frames * 4])[0::2]

if start < 0:
    # From the first loud sample.
    loud = [i for i, v in enumerate(left) if abs(v) > 500]
    if not loud:
        print("0 0 %.2f" % (frames / float(RATE)))
        sys.exit(0)
    a = loud[0]
else:
    a = int(start * RATE)
seg = left[a:a + int(length * RATE)]
peak = max([abs(v) for v in seg] or [0])
# Only over the stretches that sound, in 10 ms blocks: a runner that stalls
# as the engine starts has the mixer skip ahead past most of a short tone,
# and the silence after it is no evidence about its pitch.
BLOCK = RATE // 100
loud = [max([abs(v) for v in seg[i:i + BLOCK]]) > 500 for i in range(0, len(seg), BLOCK)]
crossings = sum(1 for i in range(1, len(seg))
                if loud[i // BLOCK] and loud[(i - 1) // BLOCK] and (seg[i-1] < 0) != (seg[i] < 0))
sounding = sum(len(seg[i * BLOCK:(i + 1) * BLOCK]) for i, b in enumerate(loud) if b)
hz = crossings / 2 / (sounding / float(RATE)) if sounding else 0
print("%.0f %d %.2f %.2f" % (hz, peak, frames / float(RATE), sounding / float(RATE)))
PY
}

start_x 24

# --------------------------------------------------------------- 1: console
say "phase 1: the console, a mode change, the shutdown and a tone"

run_engine console +set sw_mode 3 +set s_volume 1 +play smoke/tone.wav
wait_for_log console "Quake2 Initialized" || die "the engine did not start; see $work/console.log"
alive console

wait_for_screen 640x480 \
    || die "the screen is $(screen_size); vid_resizescreen should have brought 800x600 down to 640x480"
say "the engine resized the screen to 640x480"

# What the Video menu does: sw_mode changes, and the next frame picks it up.
console "sw_mode 1"
wait_for_screen 400x300 || die "after sw_mode 1 the screen is $(screen_size), not 400x300"
alive console
console "sw_mode 3"
wait_for_screen 640x480 || die "the screen did not come back to 640x480 (it is $(screen_size))"
alive console
say "a mode change at runtime took the screen to 400x300 and back"

n=$(colours console)
[ "$n" -ge 3 ] || die "the console holds $n colour(s); nothing was drawn"
say "the console holds $n distinct colours"

# Long enough that the pipe has carried a good stretch, however quickly the
# steps above went.
sleep 4
stop_engine
grep -q "Received signal 15, shutting down" "$work/console.log" \
    || die "the engine did not shut down through the signal handler"
[ -f "$work/baseq2/config.cfg" ] \
    || die "no config.cfg was written on the way out; settings would be lost"
grep -q 'sw_mode "3"' "$work/baseq2/config.cfg" \
    || die "config.cfg does not have the mode that was in use"
say "SIGTERM quit cleanly and wrote config.cfg"

set -- $(tone "$work/console.raw" -1 0.5)
[ "${3%.*}" -ge 4 ] || die "only $3 s of audio came out of the pipe"
[ "$2" -ge 500 ] || die "the pipe carried nothing but silence"
# 50 ms is twenty-odd cycles, enough to tell 440 from 220 or 880.
[ "$(echo "$4" | tr -d .)" -ge 5 ] || die "only $4 s of the tone came through; too little to measure"
# Resampling 11025 to 22050 wrong shows up as the pitch out by a factor of
# two, not by a few per cent.
[ "$1" -ge 400 ] && [ "$1" -le 480 ] || die "the tone came out at $1 Hz, not 440"
say "$3 s of audio, the tone at $1 Hz (the fixture's is 440)"

# ------------------------------------------------------------------- 2: map
# The map, under one game library: it loads, draws, saves and loads again.
# baseq2 is the single-player game; the mission packs run the same test map
# on their own libraries, which is enough to show each loads, spawns a
# player and round-trips a savegame through its own g_save code.
play_map() {
    game="$1"
    name="map-$game"
    set --
    [ "$game" = baseq2 ] || set -- +set game "$game"

    rm -f "$work/baseq2/config.cfg" "$work/$game/config.cfg"
    run_engine "$name" +set sw_mode 3 "$@" +map smoke
    wait_for_log "$name" "LoadLibrary (.*/$game/gamex86_64.so)" \
        || die "the $game game library did not load; see $work/$name.log"
    wait_for_log "$name" "client_connect\|entered the game" \
        || die "no client connected to the map under $game; see $work/$name.log"
    sleep 2
    alive "$name"

    n=$(colours "$name")
    [ "$n" -ge 3 ] || die "under $game the room holds $n colour(s); the 3D renderer drew nothing"
    say "$game: the game library loaded and the room drew in $n colours"

    console "save smoke"
    i=0
    while [ ! -f "$work/$game/save/smoke/game.ssv" ] && [ "$i" -lt 50 ]; do
        i=$((i + 1))
        sleep 0.2
    done
    [ -f "$work/$game/save/smoke/game.ssv" ] || die "$game did not save the game; see $work/$name.log"
    alive "$name"

    console "load smoke"
    wait_for_log "$name" "Loading game" || die "$game did not start loading; see $work/$name.log"
    sleep 3
    alive "$name"
    grep -q "ERROR\|Error:\|failed to load" "$work/$name.log" \
        && die "the load under $game reported an error; see $work/$name.log"
    n=$(colours "$name-loaded")
    [ "$n" -ge 3 ] || die "after the load under $game the room holds $n colour(s)"
    say "$game: saved, loaded again, and the room is back"

    stop_engine
}

say "phase 2: a map, the game libraries, and a save and a load in each"
for game in baseq2 xatrix rogue; do
    [ -x "$build/$game/gamex86_64.so" ] || die "no $build/$game/gamex86_64.so"
    play_map "$game"
done

# ----------------------------------------------------------------- 3: stall
#
# What a frame longer than the sound ring does to the sound. The ring is 0.74
# s; the 1997 mixer worked out the playback position by counting the times it
# wrapped, so a frame longer than that lost a wrap for good and every frame
# after was padded with silence. A level load is long enough. SIGSTOP is the
# shortest way to a frame that long; three seconds is four rings.
#
say "phase 3: the mixer across a three-second stall"

run_engine stall +set sw_mode 0 +set s_volume 1 +play smoke/long.wav
wait_for_log stall "Quake2 Initialized" || die "the engine did not start; see $work/stall.log"
sleep 4
alive stall
kill -STOP "$game_pid"
sleep 3
kill -CONT "$game_pid"
sleep 8
alive stall
stop_engine

set -- $(tone "$work/stall.raw" 2 2)
before="$2"
total="$3"
set -- $(tone "$work/stall.raw" 11 2)
after="$2"
[ "$before" -ge 500 ] || die "no tone before the stall (peak $before), so this phase proves nothing"
[ "$after" -ge 500 ] || die "the mixer stopped reaching the pipe after a 3 s stall (peak $before before, $after after)"
say "tone peak $before before the stall, $after after it, over $total s"

# ----------------------------------------------------------------- 4: music
#
# The game's own music directory beats $QUAKE2_MUSICDIR, which can only ever
# name one directory for every game the container can run: a mission pack's
# soundtrack goes beside its own paks. The fixture puts 880 Hz in the game's
# directory and 440 in the shared one; 880 is what should come out.
#
say "phase 4: the music directory the engine picks"

RUN_ENV="QUAKE2_MUSICDIR=$work/shared-music" \
    run_engine music +set sw_mode 0 +set cd_volume 1 +cd loop 2
wait_for_log music "CDAudio: playing" || die "no music started; see $work/music.log"
sleep 5
alive music
stop_engine

grep -q "music from $work/baseq2/music" "$work/music.log" \
    || die "the engine chose '$(grep -i 'music from' "$work/music.log" | head -1)' rather than the game's own directory"
set -- $(tone "$work/music.raw" 2 2)
[ "$2" -ge 500 ] || die "the music never reached the mixer (peak $2)"
[ "$1" -ge 800 ] && [ "$1" -le 960 ] \
    || die "the music came out at $1 Hz; the game's own directory (880) did not win"
say "music from the game's own directory, at $1 Hz"

# --------------------------------------------------------------- 5: depth 8
say "phase 5: depth 8, through a colormap"

start_x 8
rm -f "$work/baseq2/config.cfg"
run_engine depth8 +set sw_mode 3 +map smoke
wait_for_log depth8 "client_connect\|entered the game" \
    || die "the map did not load at depth 8; see $work/depth8.log"
sleep 2
alive depth8
grep -q "depth 8 visual" "$work/depth8.log" || die "the engine did not use the 8-bit visual"
n=$(colours depth8)
[ "$n" -ge 3 ] || die "at depth 8 the room holds $n colour(s)"
say "at depth 8 the room drew in $n colours"
stop_engine

# --------------------------------------------------------- 6: the re-release
#
# The 2023 re-release's pak on top of the original's, as QUAKE2_RERELEASE=1
# links it: for each file the first copy the engine can read is used, and the
# stand-in's QBSP map and 24-bit picture each give way to the original's copy
# underneath; its own map loads from it, and its demo, in its own protocol,
# plays.
say "phase 6: a re-release pak on top of the original's"
start_x 24
ln -sfn "$work/rerelease/baseq2/pak0.pak" "$work/baseq2/pak9.pak"
rm -f "$work/baseq2/config.cfg"
run_engine rerelease +set sw_mode 3 +set developer 1 +map smoke
wait_for_log rerelease "client_connect\|entered the game" \
    || die "the map did not load with the re-release pak on top; see $work/rerelease.log"
sleep 2
alive rerelease
grep -q "Added packfile .*pak9.pak" "$work/rerelease.log" || die "pak9.pak was not opened"
grep -q "maps/smoke.bsp is in the re-release's extended QBSP format" "$work/rerelease.log" \
    || die "the QBSP copy of the map was not passed over; see $work/rerelease.log"
grep -q "pics/i_health.pcx is not an 8-bit paletted PCX" "$work/rerelease.log" \
    || die "the 24-bit picture was not passed over; see $work/rerelease.log"
n=$(colours rerelease)
[ "$n" -ge 3 ] || die "with the re-release pak on top the room holds $n colour(s)"
say "the original's map and picture stood in for the re-release's; the room drew in $n colours"

console "map rr_only"
wait_for_log rerelease "pak9.pak : maps/rr_only.bsp" \
    || die "the re-release pak's own map was not loaded from it; see $work/rerelease.log"
sleep 2
alive rerelease
say "a map only the re-release pak has loaded from it"
# its sky is only there as 24-bit targas, as many of the re-release's are
grep -q "env/rrskyrt.pcx: from env/rrskyrt.tga, 256x256" "$work/rerelease.log" \
    || die "the re-release's targa sky was not taken for the .pcx; see $work/rerelease.log"
grep -q "textures/rr/wall.wal: from textures/rr/wall.tga, 64x64" "$work/rerelease.log" \
    || die "the re-release's targa texture was not taken for the .wal; see $work/rerelease.log"
n=$(colours rerelease)
[ "$n" -ge 3 ] || die "with its walls from a targa the room holds $n colour(s)"
say "its sky and walls, there only as 24-bit targas, were loaded from them; $n colours"

# The re-release's own demos are in its protocol, 2022, and are written
# as 1997's as they play (server/sv_rrdemo.c): the stand-in's goes to its
# map and brings a print with it.
DISPLAY="$disp" xdotool key grave
sleep 0.5
console_type "demomap demo1.dm2"
wait_for_log rerelease "demos/demo1.dm2 is the 2023 re-release's" \
    || die "the protocol 2022 demo was not taken for the re-release's; see $work/rerelease.log"
# its print is a key no localization file has, so it is worded from its
# name: "$smoke_rr_print" as "Rr print" (client/cl_loc.c)
wait_for_log rerelease "Rr print" \
    || die "the protocol 2022 demo did not play to its print; see $work/rerelease.log"
sleep 3
alive rerelease
say "a demo in the re-release's protocol played, as far as its print, and ended"

console_type "demomap bad.dm2"
wait_for_log rerelease "demos/bad.dm2 is in no protocol this can play" \
    || die "a demo in no known protocol was not passed over; see $work/rerelease.log"
sleep 1
alive rerelease
say "a demo in a protocol nothing has was passed over, and the engine carried on"

# The original game's demos are in protocol 26, from its first release, and
# play as they are.
console_type "demomap old.dm2"
sleep 2
grep -q "demos/old.dm2 is in protocol" "$work/rerelease.log" \
    && die "a protocol 26 demo, as the original game's are, was refused; see $work/rerelease.log"
alive rerelease
say "a demo in protocol 26, as the original game's are, was not refused"
stop_engine
rm -f "$work/baseq2/pak9.pak"

# ------------------------------------------------------------ real data
if [ -n "${QUAKE2_SMOKE_DATA:-}" ]; then
    say "phase 7: real game data from $QUAKE2_SMOKE_DATA"
    start_x 24
    # Linked into a directory of its own, as the container does, so that
    # the config.cfg and anything else the engine writes stays out of the
    # data it was pointed at.
    mkdir -p "$work/real/baseq2"
    for f in "$QUAKE2_SMOKE_DATA"/baseq2/*; do
        case "$(basename "$f")" in
            config.cfg|save|scrnshot) continue ;;
        esac
        ln -sfn "$f" "$work/real/baseq2/$(basename "$f" | tr 'A-Z' 'a-z')"
    done
    map=demo1
    [ -e "$work/real/baseq2/pak1.pak" ] && map=base1
    rm -f "$work/real.fifo" "$work/real.raw"
    mkfifo "$work/real.fifo"
    cat "$work/real.fifo" > "$work/real.raw" &
    cat_pid=$!
    DISPLAY="$disp" QUAKE2_AUDIO_FIFO="$work/real.fifo" \
        "$engine" +set basedir "$work/real" +set vid_resizescreen 1 +set sw_mode 3 \
        +map "$map" >"$work/real.log" 2>&1 &
    game_pid=$!
    wait_for_log real "entered the game\|client_connect" \
        || die "$map did not load; see $work/real.log"
    sleep 5
    alive real
    n=$(colours real)
    [ "$n" -ge 64 ] || die "$map drew in only $n colours"
    say "$map loaded and drew in $n colours"
    stop_engine
fi

say "all phases passed"
