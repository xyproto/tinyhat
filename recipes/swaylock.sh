#!/bin/sh
set -eu
umask 022
rm -rf /work/swaylock && mkdir -p /work/swaylock && cd /work/swaylock
tar --no-same-owner -xf /src/swaylock-1.8.6.tar.gz --strip-components=1
meson setup build --prefix=/usr --sysconfdir=/etc --buildtype=minsize \
	-Dpam=enabled -Dgdk-pixbuf=disabled -Dman-pages=disabled \
	-Dzsh-completions=false -Dbash-completions=false -Dfish-completions=false
ninja -C build
DESTDIR=/out/swaylock ninja -C build install
