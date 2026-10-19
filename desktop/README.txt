Quake II for the Linux desktop
==============================

id Software's 2001 GPL source release of Quake II, ported to current 64-bit
Linux: the software renderer, the single-player game, Threewave CTF and the
two mission packs, The Reckoning and Ground Zero. It
needs the game files from a copy of Quake II you own (Steam, GOG, or the
original CD), or id's free demo; none are included.

Install
-------

    ./install.sh                  for you alone, into ~/.local
    sudo ./install.sh /usr/local  for everybody on the machine

Then start Quake II from your desktop's menu, or run `quake2`
(~/.local/bin/quake2 if ~/.local/bin is not on your PATH).

It runs on Ubuntu 22.04, Debian 12, Fedora 35 or anything newer, under X11
or XWayland, and needs these libraries, which almost every desktop already
has:

    Debian, Ubuntu:  sudo apt install libasound2 libsndfile1 libxrandr2
    Fedora:          sudo dnf install alsa-lib libsndfile libXrandr
    Arch:            sudo pacman -S alsa-lib libsndfile libxrandr

Game controllers need SDL2 as well (libsdl2-2.0-0, SDL2, sdl2), which is
installed wherever Steam is. Without it the game runs, without a controller.

Game files
----------

The first time it starts, Quake II looks for a Steam or GOG copy in the
usual places. If it does not find yours:

    quake2 --data /path/to/Quake2

where /path/to/Quake2 is the directory with baseq2/pak0.pak in it (for
Steam, .../steamapps/common/Quake 2). It is remembered after that. Your game
files are only read; settings, saves and screenshots go in
~/.local/share/quake2.

A Steam copy of the 2023 re-release has the original game's baseq2 beside its
rerelease folder, and that is what plays. Started as

    QUAKE2_RERELEASE=1 quake2

the re-release's own pak goes on top of it: its remade maps, models and
textures, with the original's files standing in for whatever of it this
engine cannot read -- the console font, the maps of the N64 and Call of the
Machine campaigns. Its demos, in its own network protocol, are translated to
1997's as they play.

The music was audio tracks on the CD. A rip as track02.ogg, track03.ogg and
so on in baseq2/music is played; the GOG release's music folder is found by
itself.

Playing
-------

The first start is a 1280x720 window with WASD-style mouse look on. Options
-> Video sets the resolution (up to 1920x1200) and fullscreen; fullscreen
scales the picture up to fill the monitor, as does resizing the window.
The menus and the status bar grow with the resolution; scr_scale at the
console overrides that (1 is the original, pixel for pixel). Options has a
music volume slider beside the effects volume.
The maps' coloured light is drawn in colour; "coloured light" in Options ->
Video turns it off, as the original was, or to half.

The mouse is held while you play, and let go in the menus and the console,
and when another window has the focus. "hold mouse" in the Video menu turns
that off.

A game controller works as soon as it is plugged in. Its buttons are keys
(PAD_A, PAD_B and so on) bound in Options -> Customize Controls; the sticks
move and look. In the menus, A chooses, B goes back and Start opens and
closes them.

Escape and Backspace both open the menu. F1 shows the mission's objectives.

Sound goes to ALSA's default device, which is PipeWire or PulseAudio on a
desktop that runs either. +set s_alsadevice NAME picks another.

Anything after `quake2` is passed to the engine: `quake2 +map base1`,
`quake2 +set game ctf`. The mission packs start with
`quake2 +set game xatrix` (The Reckoning) or `quake2 +set game rogue`
(Ground Zero); New Game then starts the pack's campaign.

Uninstall
---------

    ./install.sh --uninstall [PREFIX]

Settings and saves stay in ~/.local/share/quake2.

Source, the Docker version for playing in a browser, and the full notes:
https://github.com/Krippler/Quake-2
Licensed under the GNU General Public License, version 2: see COPYING.
