#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
arch=$1
out=$2
pkgfile=$(python3 "$top/scripts/pkg.py" repofile "$top/build/repo" "$arch" linux-tinyhat)
work=$out.work
rm -rf "$work" "$out.dtbs"
mkdir -p "$work"
bsdtar -xf "$pkgfile" -C "$work" $(bsdtar -tf "$pkgfile" | grep -E '^usr/lib/modules/[^/]+/(vmlinuz|dtbs/.+)$')
for k in "$work"/usr/lib/modules/*/; do
  cp "${k}vmlinuz" "$out.new"
  if [ -d "${k}dtbs" ]; then
    mv "${k}dtbs" "$out.dtbs"
  fi
done
rm -rf "$work"
mv "$out.new" "$out"
