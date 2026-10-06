#!/bin/sh
set -eu
umask 022
CFLAGS=$(echo "$CFLAGS" | sed 's/-Os/-O3/')
CXXFLAGS=$(echo "$CXXFLAGS" | sed 's/-Os/-O3/')
export CFLAGS CXXFLAGS
rm -rf /work/zsnes && mkdir -p /work/zsnes && cd /work/zsnes
tar --no-same-owner -xf /src/zsnes-2.3.6.tar.gz --strip-components=1
make all OPT_FLAGS="$CFLAGS" WITH_AO= PREFIX=/usr
make install DESTDIR=/out/zsnes OPT_FLAGS="$CFLAGS" WITH_AO= PREFIX=/usr
