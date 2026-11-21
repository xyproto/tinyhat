#!/bin/sh
set -eu
root=$(readlink -f "$1")
tarball=$(readlink -f "$2")
stdtarball=$(readlink -f "$3")
top=$(dirname "$(dirname "$(readlink -f "$0")")")
work=$top/build/rust-dist
chmod -R u+w "$work" 2> /dev/null || true
rm -rf "$work"
mkdir -p "$work/rust" "$work/std"
bsdtar -xf "$tarball" -C "$work/rust" --strip-components 1
bsdtar -xf "$stdtarball" -C "$work/std" --strip-components 1
rm -rf "$root/usr/lib/rustlib/i686-unknown-linux-gnu" "$root/usr/lib/rustlib/i586-unknown-linux-gnu"
"$work/rust/install.sh" --prefix=/usr --destdir="$root" --disable-ldconfig \
  --components=rustc,cargo,rust-std-i686-unknown-linux-gnu > /dev/null
"$work/std/install.sh" --prefix=/usr --destdir="$root" --disable-ldconfig > /dev/null
mv -f "$root/usr/bin/cargo" "$root/usr/lib/rustlib/cargo"
install -m755 "$top/scripts/rust-i586/cargo" "$root/usr/bin/cargo"
rm -rf "$work"
