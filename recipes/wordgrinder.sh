#!/bin/sh
set -eu
umask 022
rm -rf /work/wordgrinder /out/wordgrinder && mkdir -p /work/wordgrinder && cd /work/wordgrinder
tar --no-same-owner -xf /src/wordgrinder-0.8.tar.gz --strip-components=1
make install-notests PREFIX=/usr DESTDIR=/out/wordgrinder XFT_PACKAGE=none CC=gcc CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS"
rm -rf /out/wordgrinder/usr/share/applications /out/wordgrinder/usr/share/mime-info /out/wordgrinder/usr/share/man/man1/xwordgrinder.1
