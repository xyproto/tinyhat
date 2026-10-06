#!/bin/sh
set -eu
umask 022
rm -rf /work/fontconfig /out/fontconfig && mkdir -p /work/fontconfig && cd /work/fontconfig
tar --no-same-owner -xf /src/fontconfig-2.18.3.tar.xz --strip-components=1
mkdir meson
tar --no-same-owner -xf /src/meson-1.12.1.tar.gz -C meson --strip-components=1
python3 meson/meson.py setup build --prefix=/usr --sysconfdir=/etc --localstatedir=/var --buildtype=minsize \
	-Ddoc=disabled -Dtests=disabled -Dnls=disabled -Dcache-build=disabled
ninja -C build
DESTDIR=/out/fontconfig ninja -C build install
ninja -C build install
