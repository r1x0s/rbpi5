#!/bin/sh
# prepare-assets.sh <XDJRX3-tree> -- turn YOUR already-extracted XDJ-RX3 v1.20
# firmware tree into the four inputs the build needs (docs/04-firmware-assets.md):
#
#   $ASSETS/XDJRX3/gui      the tree's gui/ (fontdata, imagedata)
#   $ASSETS/XDJRX3-gui/     images/gui.tar.gz unpacked (pset, system)
#   $ASSETS/XDJRX3-rootfs/  images/rootfs.cramfs unpacked (soft-float userland)
#   $ASSETS/rbp-audio       images/pdj.tar.gz:pdj/rbp + tools/patch-rbp
#
# <XDJRX3-tree> is the directory that holds gui/ and images/. This script does
# not obtain, decrypt or download anything: it only unpacks files you supply.
# Idempotent: re-running rebuilds the four outputs from the tree.
set -eu
. "$(dirname "$0")/lib.sh"
load_env

SRC="${1:-}"
[ -n "$SRC" ] || die "usage: make assets SRC=/path/to/XDJRX3   (the directory with gui/ and images/)"
SRC="$(cd "$SRC" && pwd)" || die "no such directory: $1"

STOCK_MD5=4f2efcfc0c9e3f539289f863acfddcc6   # stock rbp, XDJ-RX3 v1.20
AUDIO_MD5=3dda2d4e10187a75bfc16a7b4f16f192   # rbp-audio (tools/patch-rbp/PATCHES.md)

say "checking $SRC"
for f in images/rootfs.cramfs images/gui.tar.gz images/pdj.tar.gz gui/fontdata gui/imagedata; do
    [ -e "$SRC/$f" ] || die "$SRC/$f is missing -- is this the extracted XDJ-RX3 v1.20 tree?"
done
[ -f "$SRC/images/release.txt" ] && say "firmware release: $(tr -d '\r\n' < "$SRC/images/release.txt")"

build_image
mkdir -p "$ASSETS" "$WORK"
rm -rf "$WORK/assets-in" && mkdir -p "$WORK/assets-in"
cp "$SRC/images/rootfs.cramfs" "$SRC/images/pdj.tar.gz" "$WORK/assets-in/"

say "1/4 gui/ (ISO)"
rm -rf "$ASSETS/XDJRX3" && mkdir -p "$ASSETS/XDJRX3"
cp -a "$SRC/gui" "$ASSETS/XDJRX3/gui"

say "2/4 gui.tar.gz -> XDJRX3-gui/"
rm -rf "$ASSETS/XDJRX3-gui" && mkdir -p "$ASSETS/XDJRX3-gui"
tar -C "$ASSETS/XDJRX3-gui" -xzf "$SRC/images/gui.tar.gz"
[ -d "$ASSETS/XDJRX3-gui/pset" ] && [ -d "$ASSETS/XDJRX3-gui/system" ] ||
    die "gui.tar.gz did not give pset/ and system/"

say "3/4 rootfs.cramfs -> XDJRX3-rootfs/ (as root in the container: the image has device nodes)"
in_docker -root sh -c '
    set -e
    rm -rf /assets/XDJRX3-rootfs
    fsck.cramfs -b 4096 --extract=/assets/XDJRX3-rootfs /w/work/assets-in/rootfs.cramfs
    chown -R "$HOST_UID:$HOST_GID" /assets/XDJRX3-rootfs'
[ -x "$ASSETS/XDJRX3-rootfs/lib/libc.so.6" ] || [ -e "$ASSETS/XDJRX3-rootfs/lib/libc.so.6" ] ||
    die "the unpacked rootfs has no lib/libc.so.6"

say "4/4 stock rbp -> rbp-audio (interoperability patches, tools/patch-rbp)"
tar -C "$WORK/assets-in" -xzf "$WORK/assets-in/pdj.tar.gz" pdj/rbp
got="$(md5sum "$WORK/assets-in/pdj/rbp" | cut -d' ' -f1)"
[ "$got" = "$STOCK_MD5" ] || die "pdj/rbp has md5 $got, not the v1.20 stock $STOCK_MD5. The patches only fit v1.20."
in_docker python3 /w/tools/patch-rbp/rbp_patch.py /w/work/assets-in/pdj/rbp -o /assets/rbp-audio >/dev/null
got="$(md5sum "$ASSETS/rbp-audio" | cut -d' ' -f1)"
[ "$got" = "$AUDIO_MD5" ] || die "rbp-audio has md5 $got, expected $AUDIO_MD5"

rm -rf "$WORK/assets-in"
say "assets ready in $ASSETS (not tracked by git). Next: make build"
