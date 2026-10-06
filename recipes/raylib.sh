#!/bin/sh
set -eu
umask 022
rm -rf /work/raylib /out/raylib && mkdir -p /work/raylib && cd /work/raylib
tar --no-same-owner -xf /src/raylib-6.0.tar.gz --strip-components=1
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
	-DPLATFORM=SDL -DOPENGL_VERSION=2.1 -DBUILD_SHARED_LIBS=ON -DBUILD_EXAMPLES=OFF
ninja -C b
DESTDIR=/out/raylib ninja -C b install
ninja -C b install
