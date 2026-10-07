#!/bin/sh
set -eu
umask 022
rm -rf /work/glibc-x32 && mkdir -p /work/glibc-x32 && cd /work/glibc-x32
tar --no-same-owner -xf /src/glibc-2.44.tar.xz --strip-components=1
export PATH=/opt/x32/bin:$PATH
export BUILD_CC=gcc BUILD_CFLAGS="-march=i686 -mtune=generic -Os -pipe" BUILD_LDFLAGS=""
export CFLAGS="-Os -pipe -mx32" CXXFLAGS="-Os -pipe -mx32" LDFLAGS=""
mkdir build && cd build
CC=x86_64-linux-gnux32-gcc CXX=x86_64-linux-gnux32-g++ \
	../configure --host=x86_64-linux-gnux32 --prefix=/usr --libdir=/usr/libx32 \
	--with-headers=/opt/x32/usr/include --enable-kernel=5.4 \
	--disable-nscd --disable-pt_chown
make
make install_root=/opt/x32 install
