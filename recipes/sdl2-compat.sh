#!/bin/sh
set -eu
umask 022
rm -rf /work/sdl2-compat && mkdir -p /work/sdl2-compat && cd /work/sdl2-compat
tar --no-same-owner -xf /src/sdl2-compat-2.32.74.tar.gz --strip-components=1
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=/usr \
	-DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF
ninja -C b
DESTDIR=/out/sdl2-compat ninja -C b install
ninja -C b install
