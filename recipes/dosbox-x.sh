#!/bin/sh
set -eu
umask 022
CFLAGS=$(echo "$CFLAGS" | sed 's/-Os/-O3/')
CXXFLAGS=$(echo "$CXXFLAGS" | sed 's/-Os/-O3/')
export CFLAGS CXXFLAGS
rm -rf /work/dosbox-x && mkdir -p /work/dosbox-x && cd /work/dosbox-x
tar --no-same-owner -xf /src/dosbox-x-2026.10.01.tar.gz --strip-components=1
for p in /patches/dosbox-x/*.patch; do patch -Np1 -i "$p"; done
sed -i 's/fluiddrivers\[\] = {"pulseaudio", /fluiddrivers[] = {"pipewire", "pulseaudio", /' src/dosbox.cpp
sed -i 's/fluid_settings_setstr(settings, "audio.driver", "pulseaudio");/fluid_settings_setstr(settings, "audio.driver", "pipewire");/' src/gui/midi_synth.h
grep -q '"pipewire", "pulseaudio"' src/dosbox.cpp
grep -q '"audio.driver", "pipewire"' src/gui/midi_synth.h
./autogen.sh
./configure --prefix=/usr --enable-sdl2 --disable-x11 --enable-core-inline \
	--disable-opengl --disable-debug --disable-avcodec --enable-libfluidsynth --enable-libslirp \
	--disable-printer --disable-sdlnet
find . -name Makefile -exec sed -i "s/ -O2 / -O3 /g" {} +
make
make install DESTDIR=/out/dosbox-x
