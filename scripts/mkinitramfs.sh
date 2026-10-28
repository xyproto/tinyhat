#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
out=$1
work=$top/build/initramfs
rm -rf "$work"
mkdir -p "$work/bin" "$work/dev" "$work/proc" "$work/sys" "$work/run" "$work/newroot"
x() {
	p=$1
	shift
	bsdtar -xf "$(python3 "$top/scripts/pkg.py" repofiles "$top/build/repo" i686 "$p")" -C "$work" "$@"
}
x busybox usr/bin/busybox
x tinyhat-splash usr/bin/tinyhat-splash
mv "$work/usr/bin/busybox" "$work/bin/busybox"
mv "$work/usr/bin/tinyhat-splash" "$work/bin/tinyhat-splash"
chmod 755 "$work/bin/busybox" "$work/bin/tinyhat-splash"
rm -rf "$work/usr"
x util-linux usr/bin/sfdisk usr/bin/partx
x util-linux-libs 'usr/lib/libblkid.so.1*' 'usr/lib/libfdisk.so.1*' 'usr/lib/libsmartcols.so.1*' 'usr/lib/libuuid.so.1*'
x xfsprogs usr/bin/mkfs.xfs
x glibc usr/lib/libc.so.6 usr/lib/ld-linux.so.2
x libinih 'usr/lib/libinih.so.0*'
x liburcu 'usr/lib/liburcu.so.8*' 'usr/lib/liburcu-common.so.8*'
x ncurses 'usr/lib/libncursesw.so.6*'
x readline 'usr/lib/libreadline.so.8*'
ln -s usr/lib "$work/lib"
install -m755 "$top/config/initramfs/init" "$work/init"
find "$work" -exec touch -h -d "@${SOURCE_DATE_EPOCH:-315532800}" {} +
(cd "$work" && find . -mindepth 1 | LC_ALL=C sort | cpio --quiet --reproducible -o -H newc -R 0:0) | zstd -q -19 -T0 -f -o "$out"
