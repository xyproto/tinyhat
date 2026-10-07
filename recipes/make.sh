#!/bin/sh
set -eu
umask 022
rm -rf /work/make && mkdir -p /work/make && cd /work/make
tar --no-same-owner -xf /src/make-4.4.1.tar.gz --strip-components=1
./configure --prefix=/usr --without-guile
make
make DESTDIR=/out/make install
