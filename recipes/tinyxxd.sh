#!/bin/sh
set -eu
umask 022
rm -rf /work/tinyxxd /out/tinyxxd && mkdir -p /work/tinyxxd /out/tinyxxd/usr/bin /out/tinyxxd/usr/share/man/man1 && cd /work/tinyxxd
tar --no-same-owner -xf /src/tinyxxd-1.3.17.tar.gz --strip-components=1
cc -std=c99 -O2 -finline-functions -D_GNU_SOURCE -o /out/tinyxxd/usr/bin/tinyxxd main.c $LDFLAGS
ln -s tinyxxd /out/tinyxxd/usr/bin/xxd
install -m644 tinyxxd.1 /out/tinyxxd/usr/share/man/man1/tinyxxd.1
