#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
arch=$1
out=$2
modules=$3
pkgfile=$(python3 "$top/scripts/pkg.py" repofile "$top/build/repo" "$arch" linux-tinyhat)
rm -rf "$modules" "$out.dtbs"
mkdir -p "$modules/usr"
bsdtar -xpf "$pkgfile" -C "$modules" usr/lib
for k in "$modules"/usr/lib/modules/*/; do
  cp "${k}vmlinuz" "$out.new"
  rm "${k}vmlinuz"
  if [ -d "${k}dtbs" ]; then
    mv "${k}dtbs" "$out.dtbs"
  fi
done
mv "$out.new" "$out"
