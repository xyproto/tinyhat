#!/bin/sh
set -eu
umask 022
rm -rf /work/pixelc /out/pixelc && mkdir -p /work/pixelc /out/pixelc/usr/bin /out/pixelc/usr/share/pixelc && cd /work/pixelc
tar --no-same-owner -xf /src/pixelc-9b885c4865bc577131722ee248346abe55a1c410.tar.gz --strip-components=1
cc $CFLAGS -std=gnu11 -w -DPLATFORM_UNIX -Iinclude -Isrc -I/out/sdl2_image/usr/include $(pkg-config --cflags sdl2-compat) \
	-o /out/pixelc/usr/bin/pixelc $(find src -name '*.c' ! -name '*emscripten*' | sort) \
	-L/out/sdl2_image/usr/lib -lSDL2_image $(pkg-config --libs sdl2-compat) -lGL -lm $LDFLAGS
cp -r res /out/pixelc/usr/share/pixelc/
