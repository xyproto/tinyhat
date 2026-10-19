#!/bin/sh
set -eu
root=$(readlink -f "$1")
tarball=$(readlink -f "$2")
top=$(dirname "$(dirname "$(readlink -f "$0")")")
work=$top/build/rust-dist
chmod -R u+w "$work" 2>/dev/null || true
rm -rf "$work"
mkdir -p "$work"
bsdtar -xf "$tarball" -C "$work" --strip-components 1
rm -rf "$root/usr/lib/rustlib/i686-unknown-linux-gnu"
"$work/install.sh" --prefix=/usr --destdir="$root" --disable-ldconfig \
	--components=rustc,cargo,rust-std-i686-unknown-linux-gnu >/dev/null
rm -rf "$work"
