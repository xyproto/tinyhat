#!/bin/sh
set -eu
umask 022
rm -rf /work/binutils-x32 && mkdir -p /work/binutils-x32 && cd /work/binutils-x32
tar --no-same-owner -xf /src/binutils-2.45.1.tar.xz --strip-components=1
mkdir build && cd build
../configure --target=x86_64-linux-gnux32 --prefix=/opt/x32 --with-sysroot=/opt/x32 \
	--disable-nls --disable-werror --disable-multilib --disable-sim --disable-gdb --disable-gprofng
make
make install
