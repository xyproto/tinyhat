#!/bin/sh
set -eu
umask 022
rm -rf /work/blastem /out/blastem && mkdir -p /work/blastem /out/blastem/usr/lib/libretro && cd /work/blastem
tar --no-same-owner -xf /src/blastem-1e0de94dc7e669c0925a22c0fccf6cdc837af0a0.tar.gz --strip-components=1
make -f Makefile.libretro -j"$(nproc)" CC="cc -fno-asynchronous-unwind-tables"
strip --strip-unneeded blastem_libretro.so
install -m755 blastem_libretro.so /out/blastem/usr/lib/libretro/blastem_libretro.so
