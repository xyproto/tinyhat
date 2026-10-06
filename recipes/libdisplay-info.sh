#!/bin/sh
set -eu
umask 022
rm -rf /work/libdisplay-info && mkdir -p /work/libdisplay-info && cd /work/libdisplay-info
tar --no-same-owner -xf /src/libdisplay-info-0.3.0.tar.gz --strip-components=1
meson setup build --prefix=/usr --buildtype=minsize
ninja -C build
DESTDIR=/out/libdisplay-info ninja -C build install
ninja -C build install
