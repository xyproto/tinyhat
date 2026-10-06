#!/bin/sh
set -eu
umask 022
CFLAGS=$(echo "$CFLAGS" | sed 's/-Os/-O3/')
CXXFLAGS=$(echo "$CXXFLAGS" | sed 's/-Os/-O3/')
export CFLAGS CXXFLAGS
rm -rf /work/sdl3 && mkdir -p /work/sdl3 && cd /work/sdl3
tar --no-same-owner -xf /src/SDL3-3.4.18.tar.gz --strip-components=1
for p in /patches/sdl3/wayland-framebuffer.patch /patches/sdl3/dos-modes.patch; do patch -Np1 -i "$p"; done
cmake -S . -B b -G Ninja \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
	-DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF \
	-DSDL_WAYLAND=ON -DSDL_WAYLAND_LIBDECOR=OFF -DSDL_VULKAN=OFF -DSDL_RENDER_VULKAN=OFF -DSDL_GPU=OFF -DSDL_RENDER_GPU=OFF \
	-DSDL_KMSDRM=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_LIBURING=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF \
	-DSDL_X11=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_ALSA=ON -DSDL_PIPEWIRE=ON -DSDL_PULSEAUDIO=OFF \
	-DSDL_HIDAPI=ON -DSDL_HIDAPI_LIBUSB=OFF -DSDL_CAMERA=OFF -DSDL_RPATH=OFF
ninja -C b
DESTDIR=/out/sdl3 ninja -C b install
ninja -C b install
