#!/bin/sh
set -eu
umask 022
rm -rf /work/vtgbte /out/vtgbte && mkdir -p /work/vtgbte /out/vtgbte/usr/bin && cd /work/vtgbte
tar --no-same-owner -xf /src/vtGBte-1a9f460390f0e8c7ab32dd7df0f125ad23d405a9.tar.gz --strip-components=1
make CC=cc CFLAGS="$CFLAGS -I." LDFLAGS="$LDFLAGS"
install -m755 gbt /out/vtgbte/usr/bin/vtgbte
