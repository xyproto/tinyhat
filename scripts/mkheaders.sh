#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
ver=$1
src=$top/build/linux-headers-$ver
if [ ! -f "$src/Makefile" ]; then
	rm -rf "$src"
	mkdir -p "$src"
	tar -xf "$top/vendor/src/linux-$ver.tar.xz" -C "$src" --strip-components=1
fi
make -s -C "$src" ARCH=x86_64 headers_install INSTALL_HDR_PATH="$top/build/buildroot/opt/x32/usr"
