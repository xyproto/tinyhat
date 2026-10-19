#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
out=$1
work=$top/build/initramfs
rm -rf "$work"
mkdir -p "$work/bin" "$work/dev" "$work/proc" "$work/sys" "$work/run" "$work/newroot"
bsdtar -xf "$(python3 "$top/scripts/pkg.py" repofiles "$top/build/repo" i686 busybox)" -C "$work" usr/bin/busybox
bsdtar -xf "$(python3 "$top/scripts/pkg.py" repofiles "$top/build/repo" i686 tinyhat-splash)" -C "$work" usr/bin/tinyhat-splash
mv "$work/usr/bin/busybox" "$work/bin/busybox"
mv "$work/usr/bin/tinyhat-splash" "$work/bin/tinyhat-splash"
chmod 755 "$work/bin/busybox" "$work/bin/tinyhat-splash"
rm -rf "$work/usr"
install -m755 "$top/config/initramfs/init" "$work/init"
find "$work" -exec touch -h -d "@${SOURCE_DATE_EPOCH:-315532800}" {} +
(cd "$work" && find . -mindepth 1 | LC_ALL=C sort | cpio --quiet --reproducible -o -H newc -R 0:0) | zstd -q -19 -T0 -f -o "$out"
