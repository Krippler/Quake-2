#!/bin/sh
#
# Bring up a private X server, export it over VNC and noVNC, and run Quake II
# on it. Any arguments given to the container are passed straight to the
# engine, e.g. `docker run ... quake2 +map base1 +skill 2`.
#
set -eu

WIDTH="${QUAKE2_WIDTH:-640}"
HEIGHT="${QUAKE2_HEIGHT:-480}"
DATADIR="${QUAKE2_DATADIR:-/quake2data}"
STATE="${QUAKE2_STATE:-/quake2/state}"
VNC_PORT="${QUAKE2_VNC_PORT:-5900}"
WEB_PORT="${QUAKE2_WEB_PORT:-6080}"
AUDIO_PORT="${QUAKE2_AUDIO_PORT:-5901}"
PAD_PORT="${QUAKE2_PAD_PORT:-5902}"
DISP="${QUAKE2_DISPLAY:-:99}"
QUAKE2_BIN="${QUAKE2_BIN:-/usr/local/games/quake2/quake2}"
AUDIOSTREAM_BIN="${QUAKE2_AUDIOSTREAM_BIN:-/usr/local/games/quake2/audiostream}"
WSPROXY_BIN="${QUAKE2_WSPROXY_BIN:-/usr/local/bin/quake-wsproxy}"
NOVNC_ROOT="${QUAKE2_NOVNC_ROOT:-/usr/share/novnc}"
RESTART="${QUAKE2_RESTART:-1}"

# The engine's own rate, fixed in snd_stream.c. Here so the page and
# audiostream are told the same number the engine produces.
AUDIO_RATE=22050

# Every line carries the seconds since this script started, so a slow start
# says which step it was slow in: scanning a mount of spun-down disks shows up
# as a gap before "game data:", and so on.
T0=$(date +%s%N)
elapsed() {
    e=$(( ($(date +%s%N) - T0) / 100000000 ))
    printf '%d.%d' $((e / 10)) $((e % 10))
}
log() { printf '[quake2 %ss] %s\n' "$(elapsed)" "$*" >&2; }
die() { printf '[quake2 %ss] error: %s\n' "$(elapsed)" "$*" >&2; exit 1; }

##############################################################################
# Drop privileges.
#
# Started as root -- which is how Unraid and most NAS front ends run a
# container -- take ownership of the writable directory as PUID:PGID and then
# run everything else as that user. On Unraid those are 99:100 (nobody:users),
# which is what its appdata share is owned by.
#
# Started as an ordinary user already, via --user or the image default, there
# is nothing to do and PUID/PGID are ignored.
##############################################################################
if [ "$(id -u)" = "0" ]; then
    PUID="${PUID:-1001}"
    PGID="${PGID:-1001}"

    case "$PUID$PGID" in
        *[!0-9]*) die "PUID and PGID must be numeric (got '$PUID' and '$PGID')" ;;
    esac

    mkdir -p "$STATE" 2>/dev/null || true
    chown "$PUID:$PGID" "$STATE" 2>/dev/null || true

    if [ "$PUID" = "0" ] && [ "$PGID" = "0" ]; then
        # Asking to stay root. Dropping to root is not a drop, and re-executing
        # would arrive back here as root and do it again, for ever.
        log "running as root (PUID=0)"
    else
        log "running as ${PUID}:${PGID}"
        exec setpriv --reuid "$PUID" --regid "$PGID" --clear-groups "$0" "$@"
    fi
fi

# Which build this is, printed after the drop so the re-exec above does not say
# it twice. The client carries the same stamp on its start screen, and the two
# disagreeing is the whole diagnosis when a browser is quietly running an older
# client than the container it is talking to.
log "Quake II ${QUAKE2_VERSION:-dev}"

#
# How many cores this container is allowed, said out loud at startup: the
# first question anyone asks about a slow container is how much of the machine
# it was given, and the software renderer is the only thing in here that is
# genuinely CPU-hungry.
#
cpu_allowance () {
    cores=$(nproc 2>/dev/null || echo '?')

    # Pinning shows up in nproc, because it is an affinity mask. A quota does
    # not: it is a share of time across whatever cores are visible, so it has
    # to be read from the cgroup -- v2 first, then v1.
    quota=''

    if [ -r /sys/fs/cgroup/cpu.max ]; then
        read -r q p _ < /sys/fs/cgroup/cpu.max 2>/dev/null || q=max
        [ "$q" = max ] || quota=$(( q * 100 / p ))
    elif [ -r /sys/fs/cgroup/cpu/cpu.cfs_quota_us ]; then
        q=$(cat /sys/fs/cgroup/cpu/cpu.cfs_quota_us 2>/dev/null || echo -1)
        p=$(cat /sys/fs/cgroup/cpu/cpu.cfs_period_us 2>/dev/null || echo 100000)
        [ "$q" -le 0 ] 2>/dev/null || quota=$(( q * 100 / p ))
    fi

    if [ -n "$quota" ]; then
        log "${cores} core(s) visible, limited to ${quota}% of one"
    else
        log "${cores} core(s) available"
    fi
}

cpu_allowance

#
# The renderer's tables are sized for at most 1920x1200 (MAXWIDTH and
# MAXHEIGHT in ref_soft/r_local.h), and the span drawers want a width that is
# a multiple of eight. The engine clamps these itself; catching it here says
# so where it can be read.
#
case "$WIDTH$HEIGHT" in
    *[!0-9]*) die "QUAKE2_WIDTH and QUAKE2_HEIGHT must be numbers (got '$WIDTH' and '$HEIGHT')" ;;
esac

[ "$WIDTH"  -ge 320 ] || die "QUAKE2_WIDTH must be at least 320 (got $WIDTH)"
[ "$HEIGHT" -ge 240 ] || die "QUAKE2_HEIGHT must be at least 240 (got $HEIGHT)"
[ "$WIDTH"  -le 1920 ] || die "QUAKE2_WIDTH is limited to 1920 by the renderer (got $WIDTH)"
[ "$HEIGHT" -le 1200 ] || die "QUAKE2_HEIGHT is limited to 1200 by the renderer (got $HEIGHT)"

if [ $(( WIDTH % 8 )) -ne 0 ]; then
    WIDTH=$(( WIDTH / 8 * 8 ))
    log "width rounded down to $WIDTH: the span drawers work eight pixels at a time"
fi

#
# An X server's maximum screen size is fixed when it starts and RANDR can only
# move around inside it, so Xvfb is started at the largest mode the renderer
# can draw and the engine shrinks the screen to whatever resolution is in play
# (rw_x11.c, VID_SetScreenSize). That is what makes the Video menu's mode list
# work. It costs about 2 MB of the server's memory.
#
MAX_WIDTH="${QUAKE2_MAX_WIDTH:-1920}"
MAX_HEIGHT="${QUAKE2_MAX_HEIGHT:-1200}"

case "$MAX_WIDTH$MAX_HEIGHT" in
    *[!0-9]*) die "QUAKE2_MAX_WIDTH and QUAKE2_MAX_HEIGHT must be numbers (got '$MAX_WIDTH' and '$MAX_HEIGHT')" ;;
esac

[ "$MAX_WIDTH"  -le 1920 ] || die "QUAKE2_MAX_WIDTH is limited to 1920 by the renderer (got $MAX_WIDTH)"
[ "$MAX_HEIGHT" -le 1200 ] || die "QUAKE2_MAX_HEIGHT is limited to 1200 by the renderer (got $MAX_HEIGHT)"
[ "$MAX_WIDTH"  -ge "$WIDTH" ]  || MAX_WIDTH="$WIDTH"
[ "$MAX_HEIGHT" -ge "$HEIGHT" ] || MAX_HEIGHT="$HEIGHT"


##############################################################################
# The state directory holds config.cfg, savegames, logs and the links to the
# game data, so it has to be writable. A bind-mounted host directory arrives
# owned by whoever created it, which is usually not the container's user.
##############################################################################
if ! mkdir -p "$STATE" 2>/dev/null || [ ! -w "$STATE" ]; then
    log "state directory '$STATE' is not writable by uid $(id -u)"
    log ""
    log "that happens when a host directory is bind-mounted there. Either run"
    log "the container as yourself:"
    log "    docker run --user \"\$(id -u):\$(id -g)\" ..."
    log "or hand the directory to the container's user:"
    log "    chown $(id -u):$(id -g) <that directory>"
    log ""
    log "a named volume, which is what docker-compose.yml uses, needs neither."
    die "cannot write to $STATE"
fi

##############################################################################
# Assemble a game directory the engine can write to.
#
# Quake II writes config.cfg, savegames (save/) and screenshots (scrnshot/)
# into the game directory -- <basedir>/baseq2, the same directory the pak
# files are in. Mount your game data read-only, as everybody does and as the
# README says to, and the engine cannot save your game or remember a single
# setting, and does not say so where anyone would see it.
#
# So the basedir is in the state volume and the mounted files are linked into
# it. What gets written lands beside the links, in the volume, where it
# survives the container.
##############################################################################
BASEDIR="$STATE/game"
mkdir -p "$BASEDIR"

#
# Where the game data actually is. The documented layout is the mount holding
# baseq2/ directly, but a Steam or GOG install mounted whole keeps it one or two
# folders down -- and the 2023 re-release keeps its own data in rerelease/
# beside the original's baseq2, which is the one this engine can read. The
# shallowest baseq2 holding a pak0.pak wins.
#
ROOT="$DATADIR"
find_root() {
    [ -d "$DATADIR" ] || return 0
    if [ -f "$DATADIR/baseq2/pak0.pak" ] || [ -f "$DATADIR/baseq2/PAK0.PAK" ] \
       || [ -f "$DATADIR/pak0.pak" ] || [ -f "$DATADIR/PAK0.PAK" ]; then
        ROOT="$DATADIR"
        return 0
    fi
    pak=$(find "$DATADIR" -maxdepth 4 -ipath '*/baseq2/pak0.pak' \
               ! -ipath '*/rerelease/*' -printf '%d %p\n' 2>/dev/null \
          | sort -n | head -n 1 | cut -d' ' -f2-)
    if [ -n "$pak" ]; then
        ROOT=$(dirname "$(dirname "$pak")")
        log "found baseq2 at $ROOT"
    fi
}

# Every subdirectory of the root that holds pak files, or a game library, or
# maps, is a game directory: baseq2, ctf, the mission packs xatrix and rogue,
# and whatever a mod calls itself.
link_game_dir() {
    src="$1"
    name="$2"
    dest="$BASEDIR/$name"
    n=0

    mkdir -p "$dest"

    for f in "$src"/*; do
        [ -e "$f" ] || continue
        base=$(basename "$f")
        lower=$(printf '%s' "$base" | tr 'A-Z' 'a-z')
        case "$lower" in
            config.cfg|save|scrnshot)
                # The engine writes these. Linked, it would write the player's
                # settings and saves back through the link into a read-only
                # mount and fail, or into the mount itself where they do not
                # belong. Its own copies live beside the links.
                continue ;;
            pak[0-9].pak) n=$((n + 1)) ;;
            gamex86_64.so) n=$((n + 1)) ;;
            maps) n=$((n + 1)) ;;
        esac
        # Quake II opens pak0.pak to pak9.pak, lowercase, and looks for
        # everything else by lowercase name too; a CD or a Windows copy may
        # have them in capitals.
        ln -sfn "$f" "$dest/$lower"
    done

    [ "$n" -gt 0 ] && return 0
    return 1
}

FOUND_GAMES=""

#
# What is installed, as a list of game directory names. Run again before each
# start of the engine, so a mod dropped into the mount between two runs is
# there the next time. Everything it does is idempotent.
#
discover_games() {
    FOUND_GAMES=""

    find_root
    [ -d "$ROOT" ] || return 0

    for d in "$ROOT"/*; do
        [ -d "$d" ] || continue
        name=$(basename "$d" | tr 'A-Z' 'a-z')

        # Not game directories: the soundtrack (linked below), the
        # re-release's own data, and the Windows install's leftovers.
        case "$name" in
            music|rerelease|docs|3.20_changes|dxsetup|redist) continue ;;
        esac

        if link_game_dir "$d" "$name"; then
            FOUND_GAMES="$FOUND_GAMES $name"
        else
            # Not a game directory after all. Everything in there is a link
            # this script made, so removing the lot is safe -- unless the
            # engine has written into it: config.cfg and save/ are the
            # player's, and a mount that has lost its paks must not take them
            # with it.
            if [ -e "$BASEDIR/$name/config.cfg" ] || [ -d "$BASEDIR/$name/save" ]; then
                log "$name has no game files any more, but holds settings"
                log "  or savegames, so it is being left alone"
            else
                rm -rf "$BASEDIR/$name"
            fi
        fi
    done

    #
    # Paks sitting directly in the mount, with no baseq2 around them. A
    # common enough mistake that guessing is kinder than an error -- but only
    # when the scan above did not find a baseq2, or one stray pak beside a
    # perfectly good set of directories would take the whole mount over.
    #
    case " $FOUND_GAMES " in
        *" baseq2 "*) ;;
        *)
            if [ -f "$ROOT/pak0.pak" ] || [ -f "$ROOT/PAK0.PAK" ]; then
                log "found pak files directly in $ROOT; treating them as baseq2"
                link_game_dir "$ROOT" baseq2 && FOUND_GAMES="$FOUND_GAMES baseq2"
            fi
            ;;
    esac

    FOUND_GAMES=$(printf '%s' "$FOUND_GAMES" | sed 's/^ *//')

    link_rerelease
}

#
# QUAKE2_RERELEASE=1: the 2023 re-release's pak on top of the original game's.
# Steam and GOG keep it in rerelease/baseq2/ beside the original baseq2. It is
# linked into baseq2 as pak9.pak, the last the engine opens and so the first
# it searches; for each file the engine then takes the first copy it can read
# (qcommon/usable.c), and where the re-release's copy is missing -- the
# console font and the rest of the 1997 pictures -- or in a form this engine
# does not read, the original's underneath is used, so the game looks as it
# did.
#
link_rerelease() {
    slot="$BASEDIR/baseq2/pak9.pak"
    if [ "${QUAKE2_RERELEASE:-0}" != "1" ]; then
        # turned off again: take away only a link this made
        case "$(readlink "$slot" 2>/dev/null)" in
            */[Rr][Ee][Rr][Ee][Ll][Ee][Aa][Ss][Ee]/*) rm -f "$slot" ;;
        esac
        return 0
    fi

    rr=$(find "$DATADIR" -maxdepth 5 -ipath '*/rerelease/baseq2/pak0.pak' \
              -printf '%d %p\n' 2>/dev/null | sort -n | head -n 1 | cut -d' ' -f2-)
    if [ -z "$rr" ]; then
        log "QUAKE2_RERELEASE=1, but there is no rerelease/baseq2/pak0.pak in the"
        log "  mount; playing the original game's data alone"
        return 0
    fi
    case " $FOUND_GAMES " in
        *" baseq2 "*) ;;
        *)
            log "QUAKE2_RERELEASE=1, but the original game's baseq2 is not in the"
            log "  mount. The re-release's pak plays here only on top of it: it has"
            log "  no console font or 1997 pictures of its own"
            return 0
            ;;
    esac
    if [ -e "$slot" ] && [ ! -L "$slot" ]; then
        log "baseq2 has a pak9.pak of its own; not adding the re-release's"
        return 0
    fi
    if [ "$(readlink "$slot" 2>/dev/null)" != "$rr" ]; then
        ln -sfn "$rr" "$slot"
        log "the 2023 re-release's pak ($rr) is on top of the original game's"
        log "  as baseq2/pak9.pak; whatever of it this engine cannot read, the"
        log "  original's copy stands in for"
    fi
}

log "looking for game data in $DATADIR"
discover_games

case " $FOUND_GAMES " in
    *" baseq2 "*) ;;
    *)
        log ""
        log "No Quake II game data found in $DATADIR."
        log ""
        log "Quake II's data is id Software's and is not free to redistribute,"
        log "so there is none in this image. You supply your own:"
        log ""
        log "    docker run --rm -p $WEB_PORT:$WEB_PORT \\"
        log "        -v /path/to/quake2:$DATADIR:ro ghcr.io/krippler/quake-2"
        log ""
        log "where /path/to/quake2 is the directory holding baseq2/pak0.pak -- the"
        log "install directory from the CD, from Steam, from GOG, or id's demo."
        log "See README.md."
        log ""
        die "nothing to play"
        ;;
esac

log "game data: $FOUND_GAMES"

# pak0.pak alone is the demo, or a partial copy: the full game has pak1.pak
# and pak2.pak as well. Said up front, because the symptom is otherwise a
# "Couldn't load maps/base1.bsp" from the menu's New Game.
if [ -e "$BASEDIR/baseq2/pak1.pak" ]; then
    log "  baseq2: the full game"
else
    log "  baseq2: pak0.pak only -- the demo, whose maps are demo1 to demo3."
    log "  New Game in the menu wants the full game's base1."
fi

#
# Which games can actually run. The game code is a library, and this image
# carries four: the single-player game, Threewave CTF, and the two mission
# packs, The Reckoning (xatrix) and Ground Zero (rogue). A mod that brings its
# own gamex86_64.so -- built for this engine or for Yamagi Quake II, whose game
# API is the same -- runs that. Anything else falls back to the single-player
# game's code, which runs data-only mods perfectly well.
#
for g in $FOUND_GAMES; do
    case "$g" in
        baseq2|ctf|xatrix|rogue) continue ;;
    esac
    if [ ! -e "$BASEDIR/$g/gamex86_64.so" ]; then
        log "  $g: no gamex86_64.so of its own; runs on the single-player game"
    fi
done

if [ -n "${QUAKE2_GAME:-}" ] && [ "$QUAKE2_GAME" != baseq2 ]; then
    case " $FOUND_GAMES " in
        *" $QUAKE2_GAME "*) log "QUAKE2_GAME=$QUAKE2_GAME; starting there" ;;
        *) die "QUAKE2_GAME=$QUAKE2_GAME, but there is no $QUAKE2_GAME directory in the game data (found: $FOUND_GAMES)" ;;
    esac
fi

##############################################################################
# Music.
#
# Quake II's soundtrack was audio tracks 2 to 11 of the CD, so it is not in the
# pak files. cd_stream.c looks for track02.ogg and friends in <gamedir>/music
# first -- which link_game_dir has already linked in -- and then in
# QUAKE2_MUSICDIR, then baseq2/music. The GOG release keeps it in music/ at the
# top of the install, which is what this points QUAKE2_MUSICDIR at.
##############################################################################
if [ -d "$ROOT/music" ]; then
    export QUAKE2_MUSICDIR="$ROOT/music"
    log "music: $ROOT/music, shared by every game without its own"
fi

music_own=""
for g in $FOUND_GAMES; do
    [ -d "$BASEDIR/$g/music" ] || continue
    n=$(find -L "$BASEDIR/$g/music" -maxdepth 1 -type f \
             \( -iname 'track*.ogg'  -o -iname 'track*.opus' \
                -o -iname 'track*.flac' -o -iname 'track*.mp3' \
                -o -iname 'track*.wav' \) 2>/dev/null | wc -l)
    if [ "$n" -gt 0 ]; then
        log "music: $g has $n track(s) of its own"
        music_own=yes
    fi
done

if [ -z "${QUAKE2_MUSICDIR:-}" ] && [ -z "$music_own" ]; then
    log "music: none found. Put track02.ogg and friends in"
    log "       $DATADIR/baseq2/music to hear the soundtrack."
fi

##############################################################################
# Background services. Everything is torn down together.
##############################################################################
XVFB_PID=""
VNC_PID=""
WEB_PID=""
WEBLOG_PID=""
AUDIO_PID=""
GAME_PID=""
HELPERWATCH_PID=""

# Set once the stack is being torn down, so the restart loop at the bottom
# knows the engine's death was asked for rather than something to recover from.
SHUTTING_DOWN=0

cleanup() {
    trap - EXIT INT TERM
    SHUTTING_DOWN=1

    # The engine's signal handler raises a flag that the frame loop turns into
    # an ordinary quit -- writing config.cfg on the way out -- so ask it to
    # stop that way before pulling the display out from under it.
    if [ -n "$GAME_PID" ] && kill -0 "$GAME_PID" 2>/dev/null; then
        kill -TERM "$GAME_PID" 2>/dev/null || true
        i=0
        while kill -0 "$GAME_PID" 2>/dev/null && [ "$i" -lt 30 ]; do
            i=$((i + 1))
            sleep 0.1
        done
        kill -KILL "$GAME_PID" 2>/dev/null || true
    fi

    # First, so the teardown below cannot be reported as a fault.
    if [ -n "$HELPERWATCH_PID" ]; then
        kill "$HELPERWATCH_PID" 2>/dev/null || true
        HELPERWATCH_PID=""
    fi

    for pid in "$WEBLOG_PID" "$WEB_PID" "$AUDIO_PID" "$VNC_PID" "$XVFB_PID"; do
        if [ -n "$pid" ]; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

##############################################################################
# The display.
#
# The software renderer draws palette indices and translates each frame
# through the palette into whatever depth the X server has. At depth 24 there
# is no colormap anywhere: the damage flash and the underwater tint are
# palette changes, and the table is rebuilt for them. x11vnc then serves the
# screen as it finds it.
#
# QUAKE2_X_DEPTH=8 gives the renderer an 8-bit PseudoColor screen and its
# colormap instead, which costs x11vnc its most expensive piece of work
# (-8to24) and goes wrong when the engine restarts and the window is replaced.
##############################################################################
X_DEPTH="${QUAKE2_X_DEPTH:-24}"
case "$X_DEPTH" in
    8|24) ;;
    *) die "QUAKE2_X_DEPTH: '$X_DEPTH' is not 8 or 24" ;;
esac

log "starting Xvfb on $DISP at ${MAX_WIDTH}x${MAX_HEIGHT}x${X_DEPTH}, the largest mode"
log "  the renderer can draw; the engine brings the screen down to the one in play"
Xvfb "$DISP" -screen 0 "${MAX_WIDTH}x${MAX_HEIGHT}x${X_DEPTH}" -nolisten tcp -noreset \
     >"$STATE/xvfb.log" 2>&1 &
XVFB_PID=$!

sock="/tmp/.X11-unix/X${DISP#:}"
i=0
while [ ! -e "$sock" ]; do
    i=$((i + 1))
    [ "$i" -gt 100 ] && die "Xvfb failed to start; see $STATE/xvfb.log"
    kill -0 "$XVFB_PID" 2>/dev/null || die "Xvfb exited; see $STATE/xvfb.log"
    sleep 0.1
done

export DISPLAY="$DISP"

##############################################################################
# VNC.
##############################################################################
vnc_auth=""
if [ -n "${QUAKE2_VNC_PASSWORD:-}" ]; then
    x11vnc -storepasswd "$QUAKE2_VNC_PASSWORD" "$STATE/.vncpasswd" >/dev/null 2>&1
    vnc_auth="-rfbauth $STATE/.vncpasswd"
    log "VNC password authentication enabled"
else
    vnc_auth="-nopw"
    log "no VNC password set (export QUAKE2_VNC_PASSWORD to require one)"
fi

if [ "$X_DEPTH" = "24" ]; then
    vnc_8to24=""
else
    vnc_8to24="-8to24"
fi

#
# x11vnc answers an option it does not recognise by printing "unrecognized
# option(s)" into its own log and exiting, which leaves the game running, the
# container looking healthy and a browser saying "connection lost" with no
# explanation anywhere anyone thinks to look.
#
for opt in ${QUAKE2_VNC_ARGS:-}; do
    case "$opt" in
        -*) ;;
        *)  die "QUAKE2_VNC_ARGS: '$opt' does not start with a dash; x11vnc would refuse the lot" ;;
    esac
done

#
# These are the Quake port's measured settings, and the reasons are the same:
#
# -nowireframe -noscrollcopyrect: both watch for a window being dragged or a
# pane scrolled while a button is held, and hold the screen back while they
# decide. A game holds the fire button down.
#
# -xrandr resize: the engine resizes the X screen when the resolution changes
# in the Video menu, and x11vnc must rebuild its framebuffer and tell the
# client, or the picture arrives cropped.
#
# -noxdamage -wait 1 -defer 1: a game changes most of the screen every frame,
# and scanning it outright costs less than following the damage reports.
#
log "starting x11vnc on port $VNC_PORT"
# shellcheck disable=SC2086
x11vnc -display "$DISP" -rfbport "$VNC_PORT" -forever -shared -quiet \
       $vnc_8to24 \
       -nowireframe -noscrollcopyrect -noxdamage \
       -xrandr resize \
       -nonap -wait "${QUAKE2_VNC_WAIT:-1}" -defer "${QUAKE2_VNC_DEFER:-1}" \
       ${QUAKE2_VNC_ARGS:-} \
       $vnc_auth >"$STATE/x11vnc.log" 2>&1 &
VNC_PID=$!

##############################################################################
# Sound.
#
# The engine mixes everything itself -- effects in snd_mix.c, music in
# cd_stream.c -- and writes the finished stream to a FIFO. audiostream reads
# the FIFO on a real-time schedule and serves it on its own port, which the
# proxy below carries alongside the picture. audiostream has to be up first:
# it is the end that creates the FIFO.
##############################################################################
AUDIO_TO_BROWSER=0

if [ "${QUAKE2_SOUND:-1}" != "1" ]; then
    log "sound: disabled"
elif [ -x "$AUDIOSTREAM_BIN" ]; then
    AUDIO_TO_BROWSER=1
    AUDIO_FIFO="$STATE/audio.fifo"
    rm -f "$AUDIO_FIFO"

    log "starting audiostream on port $AUDIO_PORT"
    # shellcheck disable=SC2086
    "$AUDIOSTREAM_BIN" --fifo "$AUDIO_FIFO" --rate "$AUDIO_RATE" \
        --port "$AUDIO_PORT" ${QUAKE2_AUDIOSTREAM_ARGS:-} \
        >"$STATE/audiostream.log" 2>&1 &
    AUDIO_PID=$!

    i=0
    while [ ! -p "$AUDIO_FIFO" ]; do
        i=$((i + 1))
        [ "$i" -gt 100 ] && die "audiostream failed to start; see $STATE/audiostream.log"
        kill -0 "$AUDIO_PID" 2>/dev/null || die "audiostream exited; see $STATE/audiostream.log"
        sleep 0.1
    done

    export QUAKE2_AUDIO_FIFO="$AUDIO_FIFO"
    log "sound: to the browser, ${AUDIO_RATE} Hz stereo"
else
    log "sound: audiostream is missing, the game will be silent"
fi

##############################################################################
# One WebSocket port, three streams behind it: the screen, the sound and the
# controller. See docker/quake-wsproxy.py -- a connection that asks for none
# of them gets the screen, so stock /vnc.html still works.
##############################################################################
{
    printf 'vnc: localhost:%s\n' "$VNC_PORT"
    [ "$AUDIO_TO_BROWSER" = "1" ] && printf 'audio: localhost:%s\n' "$AUDIO_PORT"
    printf 'pad: localhost:%s\n' "$PAD_PORT"
} > "$STATE/ws-targets"

# The page reads the pad through the browser's Gamepad API and sends its state
# to this port; the engine listens for it there (linux/in_pad.c).
export QUAKE2_PAD_PORT="$PAD_PORT"

##############################################################################
# A config for a fresh state volume.
#
# Quake II's own default.cfg is inside pak0.pak and is id's file. The engine
# execs it and then config.cfg, so config.cfg is where an override belongs --
# and the engine rewrites config.cfg in full when it exits, so seeding it once
# is a default rather than a policy. Change anything in the game and your
# change is what persists.
#
# The resolution is always seeded, from QUAKE2_WIDTH and QUAKE2_HEIGHT: on a
# volume that already has a config, the size picked in the Video menu wins.
#
# The controls are seeded unless QUAKE2_MODERN_CONTROLS=0. What the 1997
# defaults are: the arrow keys move, `,` and `.` sidestep, `a` and `z` look up
# and down, Ctrl fires, and the mouse walks you forward unless you hold `\` to
# look with it. That was normal then. It reads as broken now.
##############################################################################
seed_config() {
    cfg="$BASEDIR/baseq2/config.cfg"

    [ -e "$cfg" ] && return 0

    {
        printf '%s\n' "// Written once, on a state volume with no config.cfg in it, and then"
        printf '%s\n' "// owned by the engine: it rewrites this file whenever it exits, so"
        printf '%s\n' "// anything changed in the menus or at the console replaces what is here."
        printf '%s\n' "// Delete the file to get these back."
        printf '\n'
        # No semicolons in these comments: Quake II splits a config into
        # commands at every semicolon, comment or not, and runs the rest.
        printf '%s\n' "// The resolution, from QUAKE2_WIDTH and QUAKE2_HEIGHT. sw_mode -1 is"
        printf '%s\n' "// whatever vid_width and vid_height say. The Video menu picks a"
        printf '%s\n' "// numbered mode instead."
        printf 'set sw_mode "-1"\n'
        printf 'set vid_width "%s"\n' "$WIDTH"
        printf 'set vid_height "%s"\n' "$HEIGHT"
    } > "$cfg"

    if [ "${QUAKE2_MODERN_CONTROLS:-1}" = "1" ]; then
        cat >> "$cfg" <<'CFGEOF'

// id's default.cfg has run by this point, so these lines only cover what
// differs. QUAKE2_MODERN_CONTROLS=0 leaves them out.

// WASD, and the mouse steering rather than walking.
set freelook "1"
set lookspring "0"
bind "w" "+forward"
bind "s" "+back"
bind "a" "+moveleft"
bind "d" "+moveright"

// Jump and crouch where a hand already is. Space was jump already.
bind "CTRL" "+movedown"

// Always run, as every game since has. Shift walks.
set cl_run "1"

// The wheel changes weapon, which needs the X11 driver to report it -- see
// the wheel handling in rw_x11.c.
bind "MWHEELUP" "weapnext"
bind "MWHEELDOWN" "weapprev"

// MOUSE2 is +strafe in id's config, which is no use once the mouse looks.
bind "MOUSE2" "+attack"

// e used the environment suit, which the inventory still does. It now uses
// whatever the inventory has selected, and f opens the inventory.
bind "e" "invuse"
bind "f" "inven"
CFGEOF
        log "seeded config.cfg with WASD and mouse look (QUAKE2_MODERN_CONTROLS=0"
        log "  starts from id's 1997 defaults instead; whatever you change in the"
        log "  game is saved over this on exit)"
    fi

    log "seeded config.cfg at ${WIDTH}x${HEIGHT}"
}

seed_config

log "starting noVNC on port $WEB_PORT"
#
# websockify's own chatter goes to its log; the lines it prints about the
# picture going quiet, and the lines the page sends about its controller and
# its sound, are lifted into this one, because that is where anyone reading a
# stutter report is looking.
#
# Through a FIFO rather than a pipeline, because `cmd | while ... &` sets $! to
# the while loop, not to cmd: the watchdog would watch the wrong process.
#
QUAKE2_RUN_ID_FILE="$STATE/run-id"
export QUAKE2_RUN_ID_FILE

WSLOG="$STATE/websockify.fifo"
rm -f "$WSLOG"
mkfifo -m 600 "$WSLOG"

"$WSPROXY_BIN" --web="$NOVNC_ROOT" \
       --token-plugin=websockify.token_plugins.ReadOnlyTokenFile \
       --token-source="$STATE/ws-targets" \
       "$WEB_PORT" >"$WSLOG" 2>&1 &
WEB_PID=$!

while IFS= read -r wsline; do
    case "$wsline" in
        "picture gap"*) log "$wsline" ;;
        "controller:"*|"sound:"*) log "$wsline" ;;
        *) printf '%s\n' "$wsline" >>"$STATE/websockify.log" ;;
    esac
done < "$WSLOG" &
WEBLOG_PID=$!

log ""
log "  play at  http://localhost:$WEB_PORT/  (or /play.html)"
log ""
log "  plain noVNC (no mouse capture):  http://localhost:$WEB_PORT/vnc.html?autoconnect=1&resize=off"
log ""

##############################################################################
# Run the game.
##############################################################################

#
# How far ahead the engine mixes sound, which is most of the delay between a
# shot being fired and being heard. id's default is 0.2 s. The Quake port
# measured keystroke to audible on the socket and settled on 0.06: below about
# 0.04 the floor is the engine's frame and the chunk size, and all a smaller
# number buys is underruns.
#
# Passed on the command line rather than seeded, because the cvar is archived:
# an existing config.cfg already has id's value and would overrule anything
# seeded. Quake II applies command-line +set after config.cfg, so this wins on
# every start. A machine that cannot keep up will underrun; raise it back
# towards 0.2 there.
#
MIXAHEAD="${QUAKE2_SND_MIXAHEAD:-0.06}"
case "$MIXAHEAD" in
    ''|*[!0-9.]*|*.*.*) die "QUAKE2_SND_MIXAHEAD must be a number of seconds (got '$MIXAHEAD')" ;;
esac

case " $* " in
    *" s_mixahead "*) ;;
    *) set -- +set s_mixahead "$MIXAHEAD" "$@" ;;
esac

# s_loadas8bit 1 is id's default, from when memory was dear: every sound
# effect is cut to 8 bits as it loads, and hisses for it.
case " $* " in
    *" s_loadas8bit "*) ;;
    *) set -- +set s_loadas8bit 0 "$@" ;;
esac

if [ -n "${QUAKE2_GAME:-}" ] && [ "$QUAKE2_GAME" != baseq2 ]; then
    case " $* " in
        *" +set game "*) ;;
        *) set -- +set game "$QUAKE2_GAME" "$@" ;;
    esac
fi

# Nothing else is on this display, so the engine may resize the screen itself
# rather than leaving a window in the corner of a framebuffer it cannot fill;
# and the Video menu hides fullscreen and the mouse grab, which mean nothing
# here.
set -- +set vid_resizescreen 1 "$@"

case " $* " in
    *" +set basedir "*) ;;
    *) set -- +set basedir "$BASEDIR" "$@" ;;
esac

cd "$BASEDIR"

#
# Notice when one of the supporting processes dies, and say so. Only the
# engine is waited on, so if x11vnc exited -- a mistyped QUAKE2_VNC_ARGS is
# enough -- the container would carry on looking healthy, with the reason in
# x11vnc's own log, which nobody knows to look at.
#
watch_helpers () {
    (
        while sleep 1; do
            for entry in "x11vnc:$VNC_PID:$STATE/x11vnc.log" \
                         "Xvfb:$XVFB_PID:$STATE/xvfb.log" \
                         "noVNC:$WEB_PID:$STATE/websockify.log" \
                         "audiostream:$AUDIO_PID:$STATE/audiostream.log"; do
                name=${entry%%:*}
                rest=${entry#*:}
                pid=${rest%%:*}
                logfile=${rest#*:}

                [ -n "$pid" ] || continue
                kill -0 "$pid" 2>/dev/null && continue

                log "$name has exited -- nothing works without it"

                if [ -n "$logfile" ] && [ -r "$logfile" ]; then
                    said=$(tail -n 40 "$logfile" 2>/dev/null \
                           | grep -iE "unrecognized|invalid|error|fatal|cannot|refused|no such" \
                           | tail -n 1)
                    [ -n "$said" ] || said=$(tail -n 2 "$logfile" 2>/dev/null)

                    printf '%s\n' "$said" | while IFS= read -r said_line; do
                        [ -n "$said_line" ] && log "  $said_line"
                    done

                    log "  the rest is in $logfile"
                fi

                # Bring the whole thing down rather than sit here looking well.
                kill "$GAME_PID" 2>/dev/null
                exit 0
            done
        done
    ) &
    HELPERWATCH_PID=$!
}

#
# Run the engine, and put it back on its feet if it falls over.
#
# A crash would otherwise take the container with it, and the page would be
# left saying "connection lost" where reloading cannot help. The display, the
# VNC server, the proxy and the sound all outlive the engine, so only the
# engine has to come back, and the browser reconnects on its own. The game
# itself starts again from the title screen; only a savegame leads back.
#
# Quitting from the menu comes back the same way and for the same reason.
#
# Not restarted: a shutdown, or an error the engine reported itself -- those
# are decisions rather than falls. And three runs in a row that end within
# seconds of starting are a container that cannot run rather than a game that
# fell over.
#
crash_runs=0

while :; do
    # Look at the mount again, so a mod added since the container started is
    # there after the next restart. Quiet unless the answer changed.
    was_found="$FOUND_GAMES"
    discover_games
    if [ "$FOUND_GAMES" != "$was_found" ]; then
        log "game data changed: $FOUND_GAMES"
    fi

    # Count the starts, so the page can tell that the engine it is looking at
    # is not the one it connected to; see the note on RUN_PATH in
    # quake-wsproxy.py.
    RUN_ID=$(( ${RUN_ID:-0} + 1 ))
    printf '%s\n' "$RUN_ID" >"$STATE/run-id" 2>/dev/null || true

    started=$(date +%s 2>/dev/null || echo 0)

    # Run in the background and wait: a foreground child would block every trap
    # until it exited, so `docker stop` could not shut the stack down.
    log "running: quake2 $*"
    "$QUAKE2_BIN" "$@" &
    GAME_PID=$!

    watch_helpers

    status=0
    wait "$GAME_PID" || status=$?
    GAME_PID=""

    if [ -n "$HELPERWATCH_PID" ]; then
        kill "$HELPERWATCH_PID" 2>/dev/null || true
        HELPERWATCH_PID=""
    fi

    case "$status" in
        132) log "Quake II hit an illegal instruction (SIGILL)" ;;
        134) log "Quake II was aborted (SIGABRT)" ;;
        136) log "Quake II hit an arithmetic error (SIGFPE)" ;;
        138) log "Quake II died on a bad address (SIGBUS)" ;;
        139) log "Quake II crashed (SIGSEGV, status 139)" ;;
        143) log "Quake II was stopped (SIGTERM)" ;;
        130) log "Quake II was interrupted (SIGINT)" ;;
        0)   log "Quake II exited" ;;
        *)   log "Quake II exited with status $status" ;;
    esac

    # Only a fall or a quit is worth getting up from.
    case "$status" in
        0)                   why="quit" ;;
        132|134|136|138|139) why="crash" ;;
        *)                   why="" ;;
    esac

    [ -n "$why" ] || break
    [ "$SHUTTING_DOWN" = "0" ] || break
    [ "$RESTART" = "1" ] || break

    now=$(date +%s 2>/dev/null || echo 0)

    if [ "$(( now - started ))" -lt 10 ]; then
        crash_runs=$(( crash_runs + 1 ))
    else
        crash_runs=0
    fi

    if [ "$crash_runs" -ge 3 ]; then
        log "that is three runs in a row that ended within seconds of starting,"
        log "so this is not something restarting will fix. Stopping, with the"
        log "reason above rather than buried under another hundred attempts."
        break
    fi

    if [ "$why" = "quit" ]; then
        log "starting the game again. Stopping the container is what stops the"
        log "container; quitting just brings you back to the title screen."
    else
        log "restarting the engine. The picture, the sound and this session all"
        log "stay up, so the browser comes back on its own -- but the game starts"
        log "again at the title screen, and only a savegame leads back to where"
        log "you were."
    fi

    log "Set QUAKE2_RESTART=0 to have the container stop instead."

    sleep 1
done

exit "$status"
