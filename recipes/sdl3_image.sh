#!/bin/sh
set -eu
umask 022
rm -rf /work/sdl3_image /out/sdl3_image && mkdir -p /work/sdl3_image && cd /work/sdl3_image
tar --no-same-owner -xf /src/SDL3_image-3.4.8.tar.gz --strip-components=1
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=/usr \
	-DSDLIMAGE_VENDORED=OFF -DSDLIMAGE_BACKEND_STB=ON -DSDLIMAGE_DEPS_SHARED=OFF \
	-DSDLIMAGE_AVIF=OFF -DSDLIMAGE_JXL=OFF -DSDLIMAGE_TIF=OFF -DSDLIMAGE_WEBP=OFF -DSDLIMAGE_SVG=OFF \
	-DSDLIMAGE_PNG_LIBPNG=OFF -DSDLIMAGE_SAMPLES=OFF -DSDLIMAGE_TESTS=OFF -DSDLIMAGE_INSTALL_MAN=OFF
ninja -C b
DESTDIR=/out/sdl3_image ninja -C b install
ninja -C b install
