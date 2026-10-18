#!/bin/sh
set -eu
umask 022
. /recipes/x32-env.sh
rm -rf /work/x32-zlib /out/x32-zlib && mkdir -p /work/x32-zlib && cd /work/x32-zlib
tar --no-same-owner -xf /src/zlib-1.3.2.tar.xz --strip-components=1
patch -Np1 -i /src/zlib-1.3.2-CVE-2026-85091.patch
CHOST=$host ./configure --prefix=/usr --libdir=/usr/libx32
make
make DESTDIR=/out/x32-zlib install
x32_finish zlib
