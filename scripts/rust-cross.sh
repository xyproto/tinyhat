#!/bin/sh
set -eu
top=$(dirname "$(dirname "$(readlink -f "$0")")")
ver=1.99.1
dest=$top/build/rust-cross
[ "$(cat "$dest/.version" 2> /dev/null)" = "$ver" ] && exit 0
mkdir -p "$top/build"
exec 9> "$top/build/.rust-cross.lock"
flock 9
[ "$(cat "$dest/.version" 2> /dev/null)" = "$ver" ] && exit 0
work=$top/build/rust-cross-work
chmod -R u+w "$work" "$dest" 2> /dev/null || true
rm -rf "$work" "$dest"
mkdir -p "$work/rust" "$work/std"
for f in rust-$ver-x86_64-unknown-linux-gnu.tar.xz rust-std-$ver-aarch64-unknown-linux-gnu.tar.xz; do
  [ -f "$top/vendor/src/$f" ] || python3 "$top/scripts/pkg.py" source "$top/vendor/src" "$f"
done
bsdtar -xf "$top/vendor/src/rust-$ver-x86_64-unknown-linux-gnu.tar.xz" -C "$work/rust" --strip-components 1
bsdtar -xf "$top/vendor/src/rust-std-$ver-aarch64-unknown-linux-gnu.tar.xz" -C "$work/std" --strip-components 1
"$work/rust/install.sh" --prefix="$dest" --disable-ldconfig \
  --components=rustc,cargo,rust-std-x86_64-unknown-linux-gnu > /dev/null
"$work/std/install.sh" --prefix="$dest" --disable-ldconfig > /dev/null
rm -rf "$work"
echo "$ver" > "$dest/.version"
