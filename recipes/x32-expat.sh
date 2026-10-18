#!/bin/sh
set -eu
umask 022
. /recipes/x32-env.sh
rm -rf /work/x32-expat /out/x32-expat && mkdir -p /work/x32-expat && cd /work/x32-expat
tar --no-same-owner -xf /src/expat-2.9.0.tar.xz --strip-components=1
./configure --host=$host --prefix=/usr --libdir=/usr/libx32 --disable-static --without-docbook --without-examples --without-tests
make
make DESTDIR=/out/x32-expat install
x32_finish expat
