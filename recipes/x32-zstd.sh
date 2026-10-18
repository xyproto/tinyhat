#!/bin/sh
set -eu
umask 022
. /recipes/x32-env.sh
rm -rf /work/x32-zstd /out/x32-zstd && mkdir -p /work/x32-zstd && cd /work/x32-zstd
tar --no-same-owner -xf /src/zstd-1.5.7.tar.zst --strip-components=1
make -C lib lib-mt
make -C lib PREFIX=/usr LIBDIR=/usr/libx32 DESTDIR=/out/x32-zstd install-shared install-includes install-pc
x32_finish zstd
