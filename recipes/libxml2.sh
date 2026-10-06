#!/bin/sh
set -eu
umask 022
rm -rf /work/libxml2 && mkdir -p /work/libxml2 && cd /work/libxml2
tar --no-same-owner -xf /src/libxml2-2.15.1.tar.xz --strip-components=1
sed -i "s/run_command('git', 'describe', check: false)/run_command('true', check: false)/" meson.build
meson setup build --prefix=/usr --buildtype=minsize \
	-Dicu=disabled -Dpython=disabled -Ddocs=disabled -Dhistory=disabled -Dreadline=disabled -Dhttp=disabled
ninja -C build
DESTDIR=/out/libxml2 ninja -C build install
