#!/bin/sh
set -eu
umask 022
rm -rf /work/sdl3_ttf /out/sdl3_ttf && mkdir -p /work/sdl3_ttf && cd /work/sdl3_ttf
tar --no-same-owner -xf /src/SDL3_ttf-3.2.2.tar.gz --strip-components=1
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=/usr \
	-DSDLTTF_VENDORED=OFF -DSDLTTF_HARFBUZZ=OFF -DSDLTTF_PLUTOSVG=OFF \
	-DSDLTTF_SAMPLES=OFF -DSDLTTF_INSTALL_MAN=OFF
ninja -C b
DESTDIR=/out/sdl3_ttf ninja -C b install
ninja -C b install
