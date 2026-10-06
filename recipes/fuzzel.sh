#!/bin/sh
set -eu
umask 022
rm -rf /work/fuzzel && mkdir -p /work/fuzzel && cd /work/fuzzel
tar --no-same-owner -xf /src/fuzzel-1.8.1.tar.gz --strip-components=1
meson setup build --prefix=/usr --buildtype=minsize -Dwerror=false \
	-Denable-cairo=disabled -Dpng-backend=libpng -Dsvg-backend=nanosvg
ninja -C build
DESTDIR=/out/fuzzel ninja -C build install
