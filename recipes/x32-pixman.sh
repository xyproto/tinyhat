#!/bin/sh
set -eu
umask 022
. /recipes/x32-env.sh
rm -rf /work/x32-pixman /out/x32-pixman && mkdir -p /work/x32-pixman && cd /work/x32-pixman
tar --no-same-owner -xf /src/pixman-0.46.4.tar.xz --strip-components=1
meson setup build --cross-file "$cross" --prefix=/usr --libdir=/usr/libx32 --buildtype=release \
	-Dlibpng=disabled -Dgtk=disabled -Dtests=disabled -Ddemos=disabled
meson compile -C build
DESTDIR=/out/x32-pixman meson install -C build
x32_finish pixman
