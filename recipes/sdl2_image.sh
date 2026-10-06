#!/bin/sh
set -eu
umask 022
rm -rf /work/sdl2_image /out/sdl2_image && mkdir -p /work/sdl2_image && cd /work/sdl2_image
tar --no-same-owner -xf /src/SDL2_image-2.8.12.tar.gz --strip-components=1
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=/usr \
	-DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_BACKEND_STB=ON -DSDL2IMAGE_DEPS_SHARED=OFF \
	-DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF -DSDL2IMAGE_SVG=OFF \
	-DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF
ninja -C b
DESTDIR=/out/sdl2_image ninja -C b install
