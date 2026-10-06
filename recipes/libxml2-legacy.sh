#!/bin/sh
set -eu
umask 022
rm -rf /work/libxml2-legacy /out/libxml2-legacy && mkdir -p /work/libxml2-legacy && cd /work/libxml2-legacy
tar --no-same-owner -xf /src/libxml2-2.13.9.tar.xz --strip-components=1
./configure --prefix=/usr --without-icu --without-python --without-readline --without-history --without-http --disable-static
make
make install DESTDIR=/work/libxml2-legacy/inst
mkdir -p /out/libxml2-legacy/usr/lib
cp -a inst/usr/lib/libxml2.so.2* /out/libxml2-legacy/usr/lib/
