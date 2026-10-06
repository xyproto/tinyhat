#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
out=$1
work=$top/build/initramfs
rm -rf "$work"
mkdir -p "$work/bin" "$work/dev" "$work/proc" "$work/sys" "$work/run" "$work/newroot"
bsdtar -xf "$top/vendor/pkg/$(awk '$2 == "busybox" { print $4 }' "$top/initramfs.lock")" -C "$work" usr/bin/busybox
mv "$work/usr/bin/busybox" "$work/bin/busybox"
rm -rf "$work/usr"
install -m755 "$top/config/initramfs/init" "$work/init"
(cd "$work" && find . -mindepth 1 | LC_ALL=C sort | cpio --quiet -o -H newc -R 0:0) | zstd -q -19 -T0 -f -o "$out"
