#!/bin/sh
set -eu
umask 022
rm -rf /work/fluidsynth && mkdir -p /work/fluidsynth && cd /work/fluidsynth
tar --no-same-owner -xf /src/fluidsynth-2.6.1.tar.gz --strip-components=1
bsdtar -xf /src/gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200.zip
rm -rf gcem && mv gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200 gcem
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DLIB_SUFFIX= \
	-Denable-sdl3=OFF -Denable-pulseaudio=OFF -Denable-pipewire=ON -Denable-jack=OFF -Denable-portaudio=OFF \
	-Denable-dbus=OFF -Denable-ladspa=OFF -Denable-network=OFF -Denable-readline=OFF -Denable-oss=OFF \
	-Denable-systemd=OFF -Denable-openmp=OFF -Denable-alsa=ON -Denable-libsndfile=ON -Denable-threads=ON
ninja -C build
DESTDIR=/out/fluidsynth ninja -C build install
ninja -C build install
