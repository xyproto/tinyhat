#!/bin/sh
set -eu
top=$(dirname "$(dirname "$(readlink -f "$0")")")
br=$top/build/buildroot-x86_64
name=$1
shift
if [ ! -x "$br/usr/bin/$name" ]; then
  echo "$name is not in $br (add its package to [buildroot] packages in distro.toml)" >&2
  exit 1
fi
exec "$br/usr/lib/ld-linux-x86-64.so.2" --library-path "$br/usr/lib" "$br/usr/bin/$name" "$@"
