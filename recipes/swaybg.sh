#!/bin/sh
set -eu
umask 022
rm -rf /work/swaybg && mkdir -p /work/swaybg && cd /work/swaybg
tar --no-same-owner -xf /src/swaybg-1.2.2.tar.gz --strip-components=1
patch -Np1 -i /patches/swaybg/help-text.patch
meson setup build --prefix=/usr --buildtype=minsize -Dgdk-pixbuf=disabled -Dman-pages=disabled
ninja -C build
DESTDIR=/out/swaybg ninja -C build install
