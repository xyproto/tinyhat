#!/bin/sh
set -eu
umask 022
. /recipes/x32-env.sh
rm -rf /work/x32-freetype /out/x32-freetype && mkdir -p /work/x32-freetype && cd /work/x32-freetype
tar --no-same-owner -xf /src/freetype-2.15.0.tar.xz --strip-components=1
./configure --host=$host --prefix=/usr --libdir=/usr/libx32 --disable-static \
	--with-zlib=yes --with-bzip2=yes --with-png=no --with-harfbuzz=no --with-brotli=no
make
make DESTDIR=/out/x32-freetype install
x32_finish freetype
