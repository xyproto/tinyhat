#!/bin/sh
set -eu
umask 022
. /recipes/x32-env.sh
rm -rf /work/x32-xz /out/x32-xz && mkdir -p /work/x32-xz && cd /work/x32-xz
tar --no-same-owner -xf /src/xz-5.8.4.tar.xz --strip-components=1
./configure --host=$host --prefix=/usr --libdir=/usr/libx32 --disable-static --disable-doc --disable-nls \
	--disable-xzdec --disable-lzmadec --disable-lzmainfo --disable-scripts
make
make DESTDIR=/out/x32-xz install
x32_finish xz
