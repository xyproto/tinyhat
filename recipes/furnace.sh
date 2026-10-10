#!/bin/sh
set -eu
umask 022
rm -rf /work/furnace /out/furnace && mkdir -p /work/furnace && cd /work/furnace
tar --no-same-owner -xf /src/furnace-0.6.8.3.tar.gz --strip-components=1
for m in fmt-e57ca2e3 adpcm-ef7a2171; do
	mkdir -p "extern/${m%-*}"
	tar --no-same-owner -xf "/src/furnace-$m.tar.gz" --strip-components=1 -C "extern/${m%-*}"
done
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE=-O2 -DCMAKE_CXX_FLAGS_RELEASE=-Os -DCMAKE_INSTALL_PREFIX=/usr \
	-DSYSTEM_SDL2=ON -DSYSTEM_ZLIB=ON -DSYSTEM_FREETYPE=ON -DUSE_RTMIDI=OFF -DWITH_JACK=OFF -DWITH_PORTAUDIO=OFF \
	-DUSE_BACKWARD=OFF -DWITH_LOCALE=OFF -DWITH_OGG=OFF -DWITH_MPEG=OFF -DWITH_DEMOS=OFF \
	-DWITH_RENDER_SDL=ON -DWITH_RENDER_OPENGL=ON -DWITH_RENDER_OPENGL1=ON -DUSE_GLES=OFF
ninja -C b
DESTDIR=/out/furnace ninja -C b install
rm -rf /out/furnace/usr/share/locale /out/furnace/usr/share/doc /out/furnace/usr/share/applications /out/furnace/usr/share/icons /out/furnace/usr/share/metainfo /out/furnace/usr/share/man
strip --strip-unneeded /out/furnace/usr/bin/furnace
