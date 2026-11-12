#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
out=$1
arch=${2:-i686}
repo=$top/build/repo
libs=$repo
ld=usr/lib/ld-linux.so.2
work=$top/build/initramfs
if [ "$arch" = aarch64 ]; then
  libs=$top/build/repo-aarch64
  ld=usr/lib/ld-linux-aarch64.so.1
  work=$top/build/$(basename "$out" .img)
fi
rm -rf "$work"
mkdir -p "$work/bin" "$work/dev" "$work/proc" "$work/sys" "$work/run" "$work/newroot"
pkgfile() { python3 "$top/scripts/pkg.py" repofile "$1" "$arch" "$2"; }
x() {
  p=$1
  shift
  bsdtar -xf "$(pkgfile "$libs" "$p")" -C "$work" "$@"
}
for p in busybox tinyhat-splash tinyhat-guru; do
  bsdtar -xf "$(pkgfile "$repo" "$p")" -C "$work" "usr/bin/$p"
  mv "$work/usr/bin/$p" "$work/bin/$p"
  chmod 755 "$work/bin/$p"
done
rm -rf "$work/usr"
x util-linux usr/bin/sfdisk usr/bin/partx usr/bin/wipefs
x util-linux-libs 'usr/lib/libblkid.so.1*' 'usr/lib/libfdisk.so.1*' 'usr/lib/libsmartcols.so.1*' 'usr/lib/libuuid.so.1*'
x xfsprogs usr/bin/mkfs.xfs
x glibc usr/lib/libc.so.6 "$ld"
x libinih 'usr/lib/libinih.so.0*'
x liburcu 'usr/lib/liburcu.so.8*' 'usr/lib/liburcu-common.so.8*'
x ncurses 'usr/lib/libncursesw.so.6*'
x readline 'usr/lib/libreadline.so.8*'
ln -s usr/lib "$work/lib"
install -m755 "$top/config/initramfs/init" "$work/init"
find "$work" -exec touch -h -d "@${SOURCE_DATE_EPOCH:-315532800}" {} +
(cd "$work" && find . -mindepth 1 | LC_ALL=C sort | cpio --quiet --reproducible -o -H newc -R 0:0) | zstd -q -19 -T0 -f -o "$out"
