# What this actually is

id Software published these sources in December 2001.
[`readme.txt`](readme.txt) is their note from that release and is left
exactly as it was -- it describes the code, not this repository.

What it does not say is that the Linux build expected gcc 2.7, an i386
assembler, a tree at `/grog/NewWork/Quake2/code` and a root-owned
`/etc/quake2.conf`, and that on a 64-bit machine it did not run once it
built. Getting from there to a playable game took:

- **Repairs to the sources themselves.** A vertex loop that stepped by the
  size of a pointer instead of a float, so every translucent surface was drawn
  from the wrong vertices and then ran off the end of its span list into the
  stack -- the first window in the first room crashed it. An MD4 whose 32-bit
  word was 64 bits, so map checksums disagreed with every other Quake II. Field
  offsets, link offsets and cache sizes taken as `int`s from addresses. A
  texture-axis loop gcc may delete. And a C sound mixer that played every
  8-bit sound -- which, by default, is every sound -- at zero volume, which
  nobody heard in 1997 because the x86 build used the assembler.
  [`PORTING-NOTES.md`](PORTING-NOTES.md) has all of them.

- **A display the renderer can draw to.** The X11 driver only knew how to
  turn its 8-bit frame into 16-bit pixels. At depth 24, which is what every
  X server is now, it drew noise. It now translates to any depth, resizes the
  container's X screen with the video mode, and scales the picture into a
  desktop window.

- **Sound without a sound card.** The 1997 backend mmaps `/dev/dsp`, which no
  current kernel has, and a container has no card at all. The engine keeps
  its own clock and writes the mix to a pipe; `audiostream` serves it to the
  page, which plays it through an `AudioWorklet`.

- **Music, which was never in the game data.** It was audio tracks on the CD,
  played by the drive. The same track numbers are looked up as files and
  mixed into the engine's output.

- **A browser client.** noVNC reports where the pointer is; a game needs how
  far it moved. `play.html` locks the pointer and walks the remote one around
  the screen by the difference. `Esc` belongs to the browser while it does, so
  Backspace is the game's menu key as well.

- **A game directory the engine can write to.** Quake II writes `config.cfg`,
  savegames and screenshots beside the pak files. The container assembles a
  writable game directory in its state volume and links the read-only data
  into it; the desktop launcher does the same in `~/.local/share/quake2`.

- **The mission packs,** The Reckoning and Ground Zero, whose game code id
  released separately in 1998 under a licence that does not allow modified
  copies. This repository carries Yamagi Quake II's GPLv2 versions instead,
  kept as they come from upstream but for two fixes the re-release's maps
  need; their game API is id's.

- **The 2023 re-release's data,** on top of the original's. Its pak has
  more files than id's pak reader took, maps whose lighting is in a lump a
  1997 engine never heard of, edges shared more widely than the renderer
  could cache, messages kept as keys to look up in `Q2Game.kpf`, doors that
  start open, and demos in an unpublished network protocol. Each is read or
  translated here, and for every file the first copy the engine can use is
  the one it takes, the original's standing in for the rest -- so the game
  looks as it did in 1997, with the re-release's maps.

- **Coloured light.** The maps' lightmaps were always in colour and the
  software renderer took only their brightness; it now lights a coloured
  surface in the palette's nearest colours, and leaves white ones exactly
  as they were.

- **A game controller,** in the browser or through SDL2 on a desktop, bound
  like any other key in the game's own menu.

The container, the browser client, `audiostream`, the controller code and
the launcher all come from [the Quake port](https://github.com/Krippler/Quake),
where the history of each decision is written up at length.

## No bundled game data

There is no game data in the image, and there will not be: it is id
Software's, and the GPL release covers the code only. Mount your own copy,
or id's demo.

## What is not here

- **The OpenGL renderer.** Its Linux glue (GLX) was never in the source
  release, only the 3Dfx and SVGAlib paths. The software renderer is the one
  this builds.
