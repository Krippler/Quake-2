# Quake II (the 2001 GPL release) in a container, playable in a browser.
#
# The software renderer draws into a private Xvfb, which is exported over VNC
# and noVNC, so the game is reachable from a browser on any host; its sound
# goes to the same page over the same port.
#
#   docker build -t quake2 .
#   docker run --rm -p 6080:6080 -v "$PWD/quake2data:/quake2data:ro" quake2
#   # then open http://localhost:6080/play.html
#
# There is no game data in this image and there will not be: it is id
# Software's, and not free to redistribute. Mount your own; see README.md.
#
# See DOCKER.md for the full set of options.

##############################################################################
# Build stage: compile the engine, the renderer and the game libraries.
##############################################################################
FROM ubuntu:24.04 AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        gcc \
        libc6-dev \
        make \
        pkg-config \
        libx11-dev \
        libxext-dev \
        libxrandr-dev \
        libsndfile1-dev \
        zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY client/ ./client/
COPY ctf/ ./ctf/
COPY xatrix/ ./xatrix/
COPY rogue/ ./rogue/
COPY game/ ./game/
COPY linux/ ./linux/
COPY qcommon/ ./qcommon/
COPY ref_soft/ ./ref_soft/
COPY server/ ./server/
COPY audiostream/ ./audiostream/

# Portable C: the 1997 assembler is 32-bit only, and q_shared.h already turns
# it off wherever __i386__ is not defined, which is here.
#
# SOUND=stream writes the mixed PCM to a pipe instead of to /dev/dsp, which no
# kernel has provided for twenty years. MUSIC=sndfile decodes track files,
# because Quake II's music was CD audio and a container has no drive.
#
# Stripped of everything but the dynamic symbol table: -rdynamic put the
# engine's function names there, and that is what a crash backtrace reads.
RUN make -C linux -j"$(nproc)" SOUND=stream MUSIC=sndfile \
 && strip --strip-unneeded linux/build/quake2 linux/build/ref_softx.so \
          linux/build/baseq2/gamex86_64.so linux/build/ctf/gamex86_64.so \
          linux/build/xatrix/gamex86_64.so linux/build/rogue/gamex86_64.so \
 && test -x linux/build/quake2

# Reads the engine's pipe and serves the sound to the browser, because the
# container has no sound card and cannot be given one.
RUN make -C audiostream -j"$(nproc)" \
 && strip audiostream/linux/audiostream \
 && test -x audiostream/linux/audiostream

##############################################################################
# Runtime stage: the engine plus a private X server and a web client.
##############################################################################
FROM ubuntu:24.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        libx11-6 \
        libxext6 \
        libxrandr2 \
        libsndfile1 \
        zlib1g \
        xvfb \
        x11vnc \
        websockify \
        tini \
# noVNC is static HTML and JavaScript served to the browser, but the distro
# package depends on Node and net-tools for tooling this image never runs.
# Unpack just the files instead; websockify above is what actually serves them.
 && apt-get download novnc \
 && dpkg-deb -x novnc_*.deb / \
 && rm -f novnc_*.deb \
# Drop what nothing depends on any more, before the forced removals below,
# which leave dpkg with unmet dependencies that apt then refuses to work around.
 && apt-get autoremove -y --purge \
# Xvfb is linked against libGL.so.1 so the dispatch library has to stay, but
# nothing in this image renders through GLX -- the renderer is in software --
# and the Mesa driver behind it costs about 180 MB, most of it LLVM.
 && dpkg --remove --force-depends \
        libglx-mesa0 mesa-libgallium libllvm20 libgl1-mesa-dri \
# websockify only uses numpy to unmask client-to-server WebSocket frames, which
# for a VNC session is the keystrokes; it warns and carries on without.
 && dpkg --remove --force-depends \
        python3-numpy liblapack3 libblas3 libgfortran5 \
 && rm -rf /var/lib/apt/lists/* /usr/share/doc/* /usr/share/man/*

# The engine finds its renderer and game libraries beside itself
# (Sys_ExeDir in linux/sys_linux.c), so they stay together.
COPY --from=build /src/linux/build/quake2 /usr/local/games/quake2/quake2
COPY --from=build /src/linux/build/ref_softx.so /usr/local/games/quake2/ref_softx.so
COPY --from=build /src/linux/build/baseq2/gamex86_64.so /usr/local/games/quake2/baseq2/gamex86_64.so
COPY --from=build /src/linux/build/ctf/gamex86_64.so /usr/local/games/quake2/ctf/gamex86_64.so
COPY --from=build /src/linux/build/xatrix/gamex86_64.so /usr/local/games/quake2/xatrix/gamex86_64.so
COPY --from=build /src/linux/build/rogue/gamex86_64.so /usr/local/games/quake2/rogue/gamex86_64.so
COPY --from=build /src/audiostream/linux/audiostream /usr/local/games/quake2/audiostream
COPY docker/entrypoint.sh /usr/local/bin/quake2-entrypoint

# websockify, taught to carry the sound and the controller alongside the
# picture on one port.
COPY docker/quake-wsproxy.py /usr/local/bin/quake-wsproxy

# Which build this is. Stamped into the client and printed at startup, so a
# report of "no sound" can be told apart from a browser quietly running the
# client from two releases ago.
ARG QUAKE2_VERSION=dev

# A client that captures the mouse, plays the sound and reads a controller.
COPY docker/play.html /usr/share/novnc/play.html
COPY docker/quake-ring.js /usr/share/novnc/quake-ring.js
COPY docker/quake-audio.js /usr/share/novnc/quake-audio.js
COPY docker/quake-gamepad.js /usr/share/novnc/quake-gamepad.js
COPY docker/index.html /usr/share/novnc/index.html

# Not sed: the stamp is whatever the build was told, and a branch name with a
# slash in it ends the s/// early. Python replaces the placeholder literally,
# and narrows the value to characters that cannot escape either the HTML text
# or the JavaScript string literal it lands in.
RUN python3 -c 'import os,re,pathlib; p=pathlib.Path("/usr/share/novnc/play.html"); v=re.sub(r"[^A-Za-z0-9._+-]","-",os.environ.get("QUAKE2_VERSION") or "dev"); s=p.read_text().replace("__QUAKE2_VERSION__",v); p.write_text(s); assert "__QUAKE2_VERSION__" not in s, "version placeholder left in the client"'

RUN chmod +x /usr/local/bin/quake2-entrypoint /usr/local/bin/quake-wsproxy \
 && mkdir -p /quake2data /quake2/state \
 && useradd --create-home --home-dir /quake2/state --uid 1001 quaker \
 && chown -R quaker:quaker /quake2

# config.cfg, savegames and screenshots are written next to the pak files.
# The entrypoint therefore builds a writable game directory inside the state
# volume and links the mounted, read-only data into it -- see
# docker/entrypoint.sh.
ENV QUAKE2_VERSION=${QUAKE2_VERSION} \
    QUAKE2_WIDTH=640 \
    QUAKE2_HEIGHT=480 \
    QUAKE2_DATADIR=/quake2data \
    QUAKE2_STATE=/quake2/state \
    QUAKE2_VNC_PORT=5900 \
    QUAKE2_AUDIO_PORT=5901 \
    QUAKE2_PAD_PORT=5902 \
    QUAKE2_WEB_PORT=6080 \
    QUAKE2_DISPLAY=:99 \
    HOME=/quake2/state

VOLUME ["/quake2/state"]
EXPOSE 6080 5900

# No USER: the entrypoint starts as root only long enough to take ownership of
# the state directory as PUID:PGID, then drops to that user for the rest.
# Defaults to the quaker account created above. Pass --user to skip that.
WORKDIR /quake2/state

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/quake2-entrypoint"]
