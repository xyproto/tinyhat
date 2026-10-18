#!/bin/sh
set -eu
umask 022
. /recipes/x32-env.sh
rm -rf /work/x32-libffi /out/x32-libffi && mkdir -p /work/x32-libffi && cd /work/x32-libffi
tar --no-same-owner -xf /src/libffi-3.8.0.tar.gz --strip-components=1
./autogen.sh
./configure --host=$host --prefix=/usr --libdir=/usr/libx32 --disable-static --disable-docs --disable-multi-os-directory
make
make DESTDIR=/out/x32-libffi install
x32_finish libffi
