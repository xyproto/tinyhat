#!/bin/sh
set -eu
umask 022
rm -rf /work/wlroots && mkdir -p /work/wlroots && cd /work/wlroots
tar --no-same-owner -xf /src/wlroots-0.19.3.tar.gz --strip-components=1
meson setup build --prefix=/usr --buildtype=minsize \
	-Dbackends=drm,libinput -Drenderers=gles2 -Dallocators=gbm -Dsession=enabled \
	-Dxwayland=disabled -Dxcb-errors=disabled -Dexamples=false \
	-Dcolor-management=disabled -Dlibliftoff=disabled
ninja -C build
DESTDIR=/out/wlroots ninja -C build install
ninja -C build install
