#!/bin/sh
#
# build-linux-tarball.sh VERSION [OUTDIR]
#
# Builds the desktop engine (SOUND=alsa) and packs it, its renderer and game
# libraries with the launcher, the menu entry and install.sh into
# quake2-linux-x86_64-VERSION.tar.gz.
#
# The binary runs on distributions with the same C library as the machine it
# is built on or newer, so the release is built on an old one: see the Linux
# job in .github/workflows/docker-publish.yml.
#

set -eu

version="${1:?usage: build-linux-tarball.sh VERSION [OUTDIR]}"
out="${2:-.}"
here=$(cd "$(dirname "$0")/.." && pwd)
arch=$(uname -m)
name="quake2-linux-$arch-$version"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

b=build-desktop
make -C "$here/linux" B=$b SOUND=alsa clean >/dev/null
make -C "$here/linux" B=$b SOUND=alsa -j"$(nproc)"
make -C "$here/linux" B=$b SOUND=alsa strip >/dev/null

root="$stage/$name"
lib="$root/lib/quake2"
install -d "$root/bin" "$lib/baseq2" "$lib/ctf" "$root/share/applications" \
           "$root/share/icons/hicolor/48x48/apps"
install -m 755 "$here/linux/$b/quake2" "$lib/quake2"
install -m 755 "$here/linux/$b/ref_softx.so" "$lib/ref_softx.so"
install -m 755 "$here/linux/$b/baseq2/gamex86_64.so" "$lib/baseq2/gamex86_64.so"
install -m 755 "$here/linux/$b/ctf/gamex86_64.so" "$lib/ctf/gamex86_64.so"
install -m 755 "$here/desktop/quake2" "$root/bin/quake2"
install -m 644 "$here/desktop/quake2.desktop" "$root/share/applications/"
install -m 644 "$here/desktop/quake2.png" "$root/share/icons/hicolor/48x48/apps/"
install -m 755 "$here/desktop/install.sh" "$root/install.sh"
install -m 644 "$here/desktop/README.txt" "$root/README.txt"
install -m 644 "$here/gnu.txt" "$root/COPYING"

mkdir -p "$out"
tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
echo "$out/$name.tar.gz"
