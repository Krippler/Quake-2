#!/usr/bin/env python3
"""Build a baseq2/pak0.pak that is enough to start Quake II and play a map in it.

The game data is not in this repository and cannot be: it is id Software's,
and not free to redistribute. So there is nothing here to smoke-test against,
and "it compiles" is not the same claim as "it runs".

This writes a pak holding what the startup path insists on -- the palette and
colormap (pics/colormap.pcx), the console font (pics/conchars.pcx) -- plus a
few tones to play and a map: one box room, built by hand as an IBSP version 38
file, with a player start in it. That is enough for the engine to load the
game library, spawn a server, connect a client to it, draw the room through
the software renderer, and save and load a game. Nothing here is game content
and none of it is id's.

    python3 tools/make-test-data.py <destdir>

writes <destdir>/baseq2/pak0.pak, and for the music tests a track 2 in two
places: <destdir>/baseq2/music/track02.wav at 880 Hz and
<destdir>/shared-music/track02.wav at 440 Hz.
"""

import math
import os
import struct
import sys


# --------------------------------------------------------------------------
# Palette.
#
# Sixteen ramps of sixteen, dark to light. What matters for a smoke test is
# that index 0 is black, that there is a grey ramp for the console text, and
# that index 255 exists: Quake II draws pictures with 255 as the transparent
# colour, not 0 as Quake did.
# --------------------------------------------------------------------------
def make_palette():
    pal = bytearray()

    def ramp(n, r0, g0, b0, r1, g1, b1):
        for i in range(n):
            t = i / (n - 1)
            pal.extend((int(r0 + (r1 - r0) * t),
                        int(g0 + (g1 - g0) * t),
                        int(b0 + (b1 - b0) * t)))

    ramp(16, 0, 0, 0, 255, 255, 255)        # 0-15   grey
    ramp(16, 15, 11, 7, 235, 195, 155)      # 16-31  browns
    ramp(16, 7, 7, 15, 175, 175, 235)       # 32-47  blues
    ramp(16, 7, 11, 7, 155, 235, 155)       # 48-63  greens
    ramp(16, 27, 7, 7, 255, 107, 107)       # 64-79  reds
    ramp(16, 19, 19, 7, 255, 255, 111)      # 80-95  yellows
    ramp(16, 27, 7, 27, 235, 131, 235)      # 96-111 magentas
    ramp(16, 7, 27, 27, 131, 235, 235)      # 112-127 cyans
    ramp(16, 43, 43, 43, 211, 211, 211)     # 128-143 another grey
    ramp(16, 63, 39, 15, 255, 183, 71)      # 144-159 fire
    ramp(16, 15, 15, 63, 71, 71, 255)       # 160-175
    ramp(16, 63, 15, 15, 255, 71, 71)       # 176-191
    ramp(16, 15, 63, 15, 71, 255, 71)       # 192-207 the menus' green
    ramp(16, 63, 63, 15, 255, 255, 71)      # 208-223
    ramp(16, 31, 31, 31, 159, 159, 159)     # 224-239
    ramp(16, 255, 255, 255, 255, 63, 63)    # 240-255
    assert len(pal) == 768, len(pal)
    return bytes(pal)


# --------------------------------------------------------------------------
# pics/colormap.pcx: 256 wide and 320 tall. The first 64 rows are the
# lighting table, row 0 fully lit and row 63 black, which is how the renderer
# reads it; the 256 below are the translucency table, entry [a][b] the colour
# to draw for b seen through a. That one is the identity here: working out a
# real blend is a nearest-colour search for each of 65536 entries, which in
# Python takes longer than the rest of the smoke test.
# --------------------------------------------------------------------------
def make_colormap():
    rows = bytearray()
    for shade in range(64):
        light = 1.0 - shade / 63.0
        for c in range(256):
            base = c & 0xf0
            step = c & 0x0f
            rows.append(base + int(step * light + 0.5))
    for a in range(256):
        rows.extend(range(256))
    assert len(rows) == 256 * 320
    return bytes(rows)


# --------------------------------------------------------------------------
# The console font: 256 cells of 8x8 in a 128x128 sheet, index 255
# transparent. The same glyphs as the Quake port's test data.
# --------------------------------------------------------------------------
GLYPHS = {
    " ": ("        ",) * 8,
    "!": ("   XX   ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "        ", "   XX   ", "        "),
    "'": ("   XX   ", "   XX   ", "   X    ", "        ", "        ", "        ", "        ", "        "),
    "(": ("     X  ", "    X   ", "   X    ", "   X    ", "   X    ", "    X   ", "     X  ", "        "),
    ")": ("  X     ", "   X    ", "    X   ", "    X   ", "    X   ", "   X    ", "  X     ", "        "),
    "*": ("        ", "  X X X ", "   XXX  ", "  XXXXX ", "   XXX  ", "  X X X ", "        ", "        "),
    "+": ("        ", "   XX   ", "   XX   ", " XXXXXX ", "   XX   ", "   XX   ", "        ", "        "),
    ",": ("        ", "        ", "        ", "        ", "        ", "   XX   ", "   XX   ", "  X     "),
    "-": ("        ", "        ", "        ", " XXXXXX ", "        ", "        ", "        ", "        "),
    ".": ("        ", "        ", "        ", "        ", "        ", "   XX   ", "   XX   ", "        "),
    "/": ("      X ", "     X  ", "    X   ", "   X    ", "  X     ", " X      ", "X       ", "        "),
    "0": ("  XXXX  ", " XX  XX ", " XX XXX ", " XXXXXX ", " XXX XX ", " XX  XX ", "  XXXX  ", "        "),
    "1": ("   XX   ", "  XXX   ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "  XXXX  ", "        "),
    "2": ("  XXXX  ", " XX  XX ", "     XX ", "    XX  ", "   XX   ", "  XX    ", " XXXXXX ", "        "),
    "3": ("  XXXX  ", " XX  XX ", "     XX ", "   XXX  ", "     XX ", " XX  XX ", "  XXXX  ", "        "),
    "4": ("    XXX ", "   XXXX ", "  XX XX ", " XX  XX ", " XXXXXX ", "     XX ", "     XX ", "        "),
    "5": (" XXXXXX ", " XX     ", " XXXXX  ", "     XX ", "     XX ", " XX  XX ", "  XXXX  ", "        "),
    "6": ("  XXXX  ", " XX  XX ", " XX     ", " XXXXX  ", " XX  XX ", " XX  XX ", "  XXXX  ", "        "),
    "7": (" XXXXXX ", "     XX ", "    XX  ", "   XX   ", "  XX    ", "  XX    ", "  XX    ", "        "),
    "8": ("  XXXX  ", " XX  XX ", " XX  XX ", "  XXXX  ", " XX  XX ", " XX  XX ", "  XXXX  ", "        "),
    "9": ("  XXXX  ", " XX  XX ", " XX  XX ", "  XXXXX ", "     XX ", " XX  XX ", "  XXXX  ", "        "),
    ":": ("        ", "   XX   ", "   XX   ", "        ", "   XX   ", "   XX   ", "        ", "        "),
    "=": ("        ", "        ", " XXXXXX ", "        ", " XXXXXX ", "        ", "        ", "        "),
    "?": ("  XXXX  ", " XX  XX ", "     XX ", "    XX  ", "   XX   ", "        ", "   XX   ", "        "),
    "[": ("   XXX  ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "   XXX  ", "        "),
    "]": ("  XXX   ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "  XXX   ", "        "),
    "_": ("        ", "        ", "        ", "        ", "        ", "        ", "        ", " XXXXXX "),
    "A": ("  XXXX  ", " XX  XX ", " XX  XX ", " XXXXXX ", " XX  XX ", " XX  XX ", " XX  XX ", "        "),
    "B": (" XXXXX  ", " XX  XX ", " XX  XX ", " XXXXX  ", " XX  XX ", " XX  XX ", " XXXXX  ", "        "),
    "C": ("  XXXX  ", " XX  XX ", " XX     ", " XX     ", " XX     ", " XX  XX ", "  XXXX  ", "        "),
    "D": (" XXXXX  ", " XX  XX ", " XX  XX ", " XX  XX ", " XX  XX ", " XX  XX ", " XXXXX  ", "        "),
    "E": (" XXXXXX ", " XX     ", " XX     ", " XXXXX  ", " XX     ", " XX     ", " XXXXXX ", "        "),
    "F": (" XXXXXX ", " XX     ", " XX     ", " XXXXX  ", " XX     ", " XX     ", " XX     ", "        "),
    "G": ("  XXXX  ", " XX  XX ", " XX     ", " XX XXX ", " XX  XX ", " XX  XX ", "  XXXX  ", "        "),
    "H": (" XX  XX ", " XX  XX ", " XX  XX ", " XXXXXX ", " XX  XX ", " XX  XX ", " XX  XX ", "        "),
    "I": ("  XXXX  ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "  XXXX  ", "        "),
    "J": ("   XXXX ", "     XX ", "     XX ", "     XX ", " XX  XX ", " XX  XX ", "  XXXX  ", "        "),
    "K": (" XX  XX ", " XX XX  ", " XXXX   ", " XXX    ", " XXXX   ", " XX XX  ", " XX  XX ", "        "),
    "L": (" XX     ", " XX     ", " XX     ", " XX     ", " XX     ", " XX     ", " XXXXXX ", "        "),
    "M": (" XX  XX ", " XXXXXX ", " XXXXXX ", " XXXXXX ", " XX  XX ", " XX  XX ", " XX  XX ", "        "),
    "N": (" XX  XX ", " XXX XX ", " XXXXXX ", " XXXXXX ", " XX XXX ", " XX  XX ", " XX  XX ", "        "),
    "O": ("  XXXX  ", " XX  XX ", " XX  XX ", " XX  XX ", " XX  XX ", " XX  XX ", "  XXXX  ", "        "),
    "P": (" XXXXX  ", " XX  XX ", " XX  XX ", " XXXXX  ", " XX     ", " XX     ", " XX     ", "        "),
    "Q": ("  XXXX  ", " XX  XX ", " XX  XX ", " XX  XX ", " XX XXX ", " XX  XX ", "  XXXXX ", "        "),
    "R": (" XXXXX  ", " XX  XX ", " XX  XX ", " XXXXX  ", " XXXX   ", " XX XX  ", " XX  XX ", "        "),
    "S": ("  XXXX  ", " XX  XX ", " XX     ", "  XXXX  ", "     XX ", " XX  XX ", "  XXXX  ", "        "),
    "T": (" XXXXXX ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "   XX   ", "        "),
    "U": (" XX  XX ", " XX  XX ", " XX  XX ", " XX  XX ", " XX  XX ", " XX  XX ", "  XXXX  ", "        "),
    "V": (" XX  XX ", " XX  XX ", " XX  XX ", " XX  XX ", " XX  XX ", "  XXXX  ", "   XX   ", "        "),
    "W": (" XX  XX ", " XX  XX ", " XX  XX ", " XXXXXX ", " XXXXXX ", " XXXXXX ", " XX  XX ", "        "),
    "X": (" XX  XX ", " XX  XX ", "  XXXX  ", "   XX   ", "  XXXX  ", " XX  XX ", " XX  XX ", "        "),
    "Y": (" XX  XX ", " XX  XX ", " XX  XX ", "  XXXX  ", "   XX   ", "   XX   ", "   XX   ", "        "),
    "Z": (" XXXXXX ", "     XX ", "    XX  ", "   XX   ", "  XX    ", " XX     ", " XXXXXX ", "        "),
}


def make_conchars():
    """128x128, 16x16 cells of 8x8. Index 255 is the transparent colour."""
    sheet = bytearray([255]) * (128 * 128)

    def draw(cell, rows, colour):
        cx = (cell % 16) * 8
        cy = (cell // 16) * 8
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch != " ":
                    sheet[(cy + y) * 128 + cx + x] = colour

    for code in range(32, 127):
        ch = chr(code)
        rows = GLYPHS.get(ch) or GLYPHS.get(ch.upper())
        if rows is None:
            rows = ("        ", " XXXXXX ", " X    X ", " X    X ",
                    " X    X ", " X    X ", " XXXXXX ", "        ")
        draw(code, rows, 15)
        # The high half is the same font in green, which is how Quake II
        # marks text in its menus.
        draw(code + 128, rows, 207)

    # The console's cursor and the menus' slider ends.
    draw(11, ("        ", " XXXXXX ", " XXXXXX ", " XXXXXX ",
              " XXXXXX ", " XXXXXX ", " XXXXXX ", "        "), 15)
    for cell in (128, 129, 130, 131, 13):
        draw(cell, GLYPHS["-"], 15)
    return bytes(sheet)


def pcx(width, height, pixels, palette):
    """An 8-bit, run-length encoded PCX with the palette at the end, which is
    the only kind LoadPCX accepts."""
    out = bytearray()
    out += struct.pack("<BBBBhhhhhh", 0x0a, 5, 1, 8, 0, 0, width - 1, height - 1,
                       72, 72)
    out += bytes(48)                     # the EGA palette nobody reads
    out += struct.pack("<BBhhhh", 0, 1, width, 1, 0, 0)
    out += bytes(54)
    assert len(out) == 128
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        x = 0
        while x < width:
            v = row[x]
            n = 1
            while x + n < width and row[x + n] == v and n < 63:
                n += 1
            if n > 1 or v >= 0xc0:
                out += bytes((0xc0 | n, v))
            else:
                out.append(v)
            x += n
    out.append(0x0c)
    out += palette
    return bytes(out)


def make_wav(seconds=0.6, freq=440.0, rate=11025, sustain=False, bits=8):
    """A mono RIFF. A tone rather than silence, so the smoke test can tell
    "the mixer ran" from "the mixer ran and produced nothing".

    11025 Hz by default, which is half the rate the engine mixes at, so the
    pitch coming out right shows the resampler is right too. sustain holds the
    level to the end rather than decaying, for the phase that asks whether
    sound is still reaching the pipe some seconds after a stall.
    """
    n = int(rate * seconds)
    samples = bytearray()
    for i in range(n):
        env = min(1.0, i / (rate * 0.01))
        if not sustain:
            env *= 1.0 - i / n
        v = math.sin(2 * math.pi * freq * i / rate) * env
        if bits == 8:
            samples.append(max(0, min(255, int(128 + v * 100))))
        else:
            samples += struct.pack("<h", int(v * 20000))

    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate * bits // 8, bits // 8, bits)
    body = (b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt +
            b"data" + struct.pack("<I", len(samples)) + bytes(samples))
    return b"RIFF" + struct.pack("<I", len(body)) + body


# --------------------------------------------------------------------------
# The map: a closed box, 512 by 512 and 256 high, centred on the origin with
# its floor at z = 0.
#
# Built the way qbsp would leave it, by hand. The BSP tree is a chain of six
# nodes, one per wall, each on an axial plane with a positive normal (the
# engine reads an axial plane's distance straight off one coordinate, so the
# normal has to be +x, +y or +z for that to come out right). Whichever side
# of each wall is outside goes to leaf 0, which is solid, as the engine
# requires of leaf 0; what is inside all six is leaf 1, the room, cluster 0.
#
# Collision is brushes, not the tree, so the walls are six solid slabs too,
# listed in leaf 0. Their sides face outward, which for half of them is a
# negative normal -- given a non-axial plane type so that nothing takes the
# shortcut above with them.
#
# The faces have no lightmap (lightofs -1), which the renderer draws at full
# brightness, and a texture that is not in the pak, which it draws as its
# checkerboard: both are what a broken map looks like, which is fine, because
# what this is testing is that the engine draws what it is given.
#
# A face wider than 256 texels needs subdividing in the software renderer
# ("Bad surface extents"), so the texture axes are scaled by a half: the
# 512-unit walls are 256 texels across.
# --------------------------------------------------------------------------
LUMPS = 19
CONTENTS_SOLID = 1
ROOM = 256          # half the width
HEIGHT = 256
THICK = 16


def make_bsp():
    planes = []

    def plane(normal, dist):
        if normal in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
            ptype = normal.index(1)
        else:
            ptype = 3 + [abs(c) for c in normal].index(1)
        key = (normal, dist)
        for i, p in enumerate(planes):
            if p[:2] == key:
                return i
        planes.append((normal, dist, ptype))
        return len(planes) - 1

    R, H = ROOM, HEIGHT

    # The room's corners.
    verts = [(x, y, z) for z in (0, H) for y in (-R, R) for x in (-R, R)]
    #   0 (-R,-R,0) 1 (R,-R,0) 2 (-R,R,0) 3 (R,R,0)
    #   4 (-R,-R,H) 5 (R,-R,H) 6 (-R,R,H) 7 (R,R,H)

    # Each wall: its node plane, which side of that plane the room is on
    # (0 = in front), its corners in the order qbsp winds a face -- clockwise
    # seen from the side it faces -- and its texture axes.
    half = 0.5
    walls = [
        # floor, plane z=0, room in front, seen from above
        (plane((0, 0, 1), 0), 0, [0, 2, 3, 1], ((half, 0, 0), (0, -half, 0))),
        # ceiling, plane z=H, room behind, seen from below
        (plane((0, 0, 1), H), 1, [4, 5, 7, 6], ((half, 0, 0), (0, half, 0))),
        # west wall, plane x=-R, room in front
        (plane((1, 0, 0), -R), 0, [0, 4, 6, 2], ((0, half, 0), (0, 0, -half))),
        # east wall, plane x=R, room behind
        (plane((1, 0, 0), R), 1, [1, 3, 7, 5], ((0, -half, 0), (0, 0, -half))),
        # south wall, plane y=-R, room in front
        (plane((0, 1, 0), -R), 0, [0, 1, 5, 4], ((-half, 0, 0), (0, 0, -half))),
        # north wall, plane y=R, room behind
        (plane((0, 1, 0), R), 1, [2, 6, 7, 3], ((half, 0, 0), (0, 0, -half))),
    ]

    # Edges and surfedges. Edge 0 is never used: a surfedge's sign says which
    # way round the edge is walked, and -0 is 0.
    edges = [(0, 0)]
    surfedges = []
    faces = []
    texinfos = []
    for planenum, side, corners, (s, t) in walls:
        first = len(surfedges)
        for i in range(len(corners)):
            a, b = corners[i], corners[(i + 1) % len(corners)]
            for e, (ea, eb) in enumerate(edges):
                if e and (ea, eb) == (b, a):
                    surfedges.append(-e)
                    break
            else:
                edges.append((a, b))
                surfedges.append(len(edges) - 1)
        texinfos.append(struct.pack("<4f4fii32si", *s, 0.0, *t, 0.0, 0, 0,
                                    b"smoke/wall", -1))
        faces.append(struct.pack("<HhihhBBBBi", planenum, side, first,
                                 len(corners), len(texinfos) - 1,
                                 0, 255, 255, 255, -1))

    # The tree. A child >= 0 is a node; -(n + 1) is leaf n.
    SOLID, ROOMLEAF = -1, -2
    lo = (-R - THICK, -R - THICK, -THICK)
    hi = (R + THICK, R + THICK, H + THICK)
    nodes = []
    for i, (planenum, side, _, _) in enumerate(walls):
        inner = i + 1 if i + 1 < len(walls) else ROOMLEAF
        children = (inner, SOLID) if side == 0 else (SOLID, inner)
        nodes.append(struct.pack("<iii3h3hHH", planenum, *children,
                                 *lo, *hi, i, 1))
    # Walk order: the chain above visits floor, ceiling, west, east, south,
    # north, which is the order the faces were made in, so node i owns face i.

    # The slabs, each a box of six outward-facing sides.
    slabs = [
        ((-R - THICK, -R - THICK, -THICK), (R + THICK, R + THICK, 0)),
        ((-R - THICK, -R - THICK, H), (R + THICK, R + THICK, H + THICK)),
        ((-R - THICK, -R - THICK, -THICK), (-R, R + THICK, H + THICK)),
        ((R, -R - THICK, -THICK), (R + THICK, R + THICK, H + THICK)),
        ((-R - THICK, -R - THICK, -THICK), (R + THICK, -R, H + THICK)),
        ((-R - THICK, R, -THICK), (R + THICK, R + THICK, H + THICK)),
    ]
    brushes = []
    brushsides = []
    for mn, mx in slabs:
        first = len(brushsides)
        for axis in range(3):
            n = [0, 0, 0]
            n[axis] = 1
            brushsides.append(struct.pack("<Hh", plane(tuple(n), mx[axis]), 0))
            n[axis] = -1
            brushsides.append(struct.pack("<Hh", plane(tuple(n), -mn[axis]), 0))
        brushes.append(struct.pack("<iii", first, 6, CONTENTS_SOLID))

    leafs = [
        # leaf 0: solid, every slab in it
        struct.pack("<ihh3h3hHHHH", CONTENTS_SOLID, -1, 0, *lo, *hi,
                    0, 0, 0, len(slabs)),
        # leaf 1: the room, cluster 0, area 1, every face visible from it
        struct.pack("<ihh3h3hHHHH", 0, 0, 1, -R, -R, 0, R, R, H,
                    0, len(faces), 0, 0),
    ]
    leaffaces = struct.pack("<%dH" % len(faces), *range(len(faces)))
    leafbrushes = struct.pack("<%dH" % len(slabs), *range(len(slabs)))

    # One cluster, which can see and hear itself. The rows are run-length
    # encoded, and a lone 0x01 byte is a row whose first bit is set.
    vis = struct.pack("<iii", 1, 12, 13) + b"\x01\x01"

    models = struct.pack("<3f3f3fiii", *lo, *hi, 0, 0, 0, 0, 0, len(faces))

    # Area 0 is the outside, which no leaf is in; area 1 is the room.
    areas = struct.pack("<ii", 0, 0) * 2

    entities = (b'{\n"classname" "worldspawn"\n"message" "Smoke Test"\n}\n'
                b'{\n"classname" "info_player_start"\n"origin" "0 0 40"\n'
                b'"angle" "45"\n}\n\0')

    lumps = [
        entities,
        b"".join(struct.pack("<3ffi", *map(float, n), float(d), t)
                 for n, d, t in planes),
        b"".join(struct.pack("<3f", *map(float, v)) for v in verts),
        vis,
        b"".join(nodes),
        b"".join(texinfos),
        b"".join(faces),
        b"",                                    # lighting: none, so full bright
        b"".join(leafs),
        leaffaces,
        leafbrushes,
        b"".join(struct.pack("<HH", *e) for e in edges),
        struct.pack("<%di" % len(surfedges), *surfedges),
        models,
        b"".join(brushes),
        b"".join(brushsides),
        bytes(256),                             # pop: the demo's copy check
        areas,
        b"",                                    # areaportals: none
    ]
    assert len(lumps) == LUMPS

    out = bytearray(b"IBSP" + struct.pack("<i", 38) + bytes(LUMPS * 8))
    for i, data in enumerate(lumps):
        while len(out) % 4:
            out.append(0)
        struct.pack_into("<ii", out, 8 + i * 8, len(out), len(data))
        out += data
    return bytes(out)


def build_pak(files):
    """A PACK: the directory at the end, 64 bytes an entry, names of 56."""
    body = bytearray()
    directory = bytearray()
    for name, data in files:
        assert len(name) < 56, name
        directory += struct.pack("<56sii", name.encode(), 12 + len(body), len(data))
        body += data
    header = b"PACK" + struct.pack("<ii", 12 + len(body), len(directory))
    return header + bytes(body) + bytes(directory)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make-test-data.py <destdir>")
    dest = sys.argv[1]

    palette = make_palette()
    files = [
        ("pics/colormap.pcx", pcx(256, 320, make_colormap(), palette)),
        ("pics/conchars.pcx", pcx(128, 128, make_conchars(), palette)),
        # The pictures the status bar and the console draw every frame.
        # Missing, each is a "Can't find pic" line per frame, which buries
        # the log; these are flat colour, which is all the test needs.
        ("pics/conback.pcx", pcx(64, 64, bytes([128 + (y // 8) % 2 * 4 for y in range(64) for x in range(64)]), palette)),
        ("pics/i_health.pcx", pcx(24, 24, bytes([79]) * 576, palette)),
        ("pics/w_blaster.pcx", pcx(24, 24, bytes([95]) * 576, palette)),
        ("pics/inventory.pcx", pcx(256, 192, bytes([136]) * (256 * 192), palette)),
    ] + [
        ("pics/%s%s.pcx" % (prefix, digit), pcx(16, 24, bytes([colour]) * 384, palette))
        for prefix, colour in (("num_", 15), ("anum_", 79))
        for digit in [str(d) for d in range(10)] + ["minus"]
    ] + [
        ("sound/smoke/tone.wav", make_wav()),
        ("sound/smoke/long.wav", make_wav(seconds=30, sustain=True)),
        ("maps/smoke.bsp", make_bsp()),
        # Say nothing at startup that the test does not ask for: id's own
        # default.cfg binds keys, and an empty one is quieter than its absence.
        ("default.cfg", b"// smoke test\n"),
    ]

    os.makedirs(os.path.join(dest, "baseq2"), exist_ok=True)
    with open(os.path.join(dest, "baseq2", "pak0.pak"), "wb") as f:
        f.write(build_pak(files))

    # Track 2, twice: the game's own music directory, which should win, at
    # 880 Hz, and a shared one for $QUAKE2_MUSICDIR at 440.
    for sub, freq in (("baseq2/music", 880.0), ("shared-music", 440.0)):
        os.makedirs(os.path.join(dest, sub), exist_ok=True)
        with open(os.path.join(dest, sub, "track02.wav"), "wb") as f:
            f.write(make_wav(seconds=20, freq=freq, rate=44100, sustain=True,
                             bits=16))

    # A stand-in for the 2023 re-release's pak, which goes on top of the
    # one above as baseq2/pak9.pak (docker/entrypoint.sh): the same paths in
    # forms this engine cannot read -- the map in the extended QBSP format, a
    # 24-bit picture, a demo in protocol 2023 -- no console font, a
    # default.cfg for its own engine, and one map of its own that is fine.
    bsp = make_bsp()
    rgb = bytearray(pcx(24, 24, bytes([0]) * 576, palette))
    rgb[65] = 3                     # three colour planes: 24-bit
    demo = struct.pack("<iBi", 5, 12, 2023)     # svc_serverdata, protocol 2023
    rerelease = [
        ("maps/smoke.bsp", b"QBSP" + bsp[4:]),
        ("maps/rr_only.bsp", bsp),
        ("pics/i_health.pcx", bytes(rgb)),
        ("demos/demo1.dm2", demo + struct.pack("<i", -1)),
        ("default.cfg", b"bind a_button +attack\n"),
    ]
    os.makedirs(os.path.join(dest, "rerelease", "baseq2"), exist_ok=True)
    with open(os.path.join(dest, "rerelease", "baseq2", "pak0.pak"), "wb") as f:
        f.write(build_pak(rerelease))

    for name, data in files:
        print("%-24s %8d bytes" % (name, len(data)))


if __name__ == "__main__":
    main()
