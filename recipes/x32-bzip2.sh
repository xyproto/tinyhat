#!/bin/sh
set -eu
umask 022
. /recipes/x32-env.sh
rm -rf /work/x32-bzip2 /out/x32-bzip2 && mkdir -p /work/x32-bzip2 && cd /work/x32-bzip2
tar --no-same-owner -xf /src/bzip2-1.0.8.tar.gz --strip-components=1
make -f Makefile-libbz2_so CC="$CC" CFLAGS="$CFLAGS -fPIC -D_FILE_OFFSET_BITS=64"
mkdir -p /out/x32-bzip2/usr/libx32 /out/x32-bzip2/usr/include /out/x32-bzip2/usr/libx32/pkgconfig
install -m755 libbz2.so.1.0.8 /out/x32-bzip2/usr/libx32/libbz2.so.1.0.8
ln -s libbz2.so.1.0.8 /out/x32-bzip2/usr/libx32/libbz2.so.1.0
ln -s libbz2.so.1.0.8 /out/x32-bzip2/usr/libx32/libbz2.so.1
ln -s libbz2.so.1.0.8 /out/x32-bzip2/usr/libx32/libbz2.so
install -m644 bzlib.h /out/x32-bzip2/usr/include/bzlib.h
printf "prefix=/usr\nlibdir=\${prefix}/libx32\nincludedir=\${prefix}/include\n\nName: bzip2\nDescription: A file compression library\nVersion: 1.0.8\nLibs: -L\${libdir} -lbz2\nCflags: -I\${includedir}\n" >/out/x32-bzip2/usr/libx32/pkgconfig/bzip2.pc
x32_finish bzip2
