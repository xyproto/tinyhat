#!/bin/sh
set -eu
umask 022
rm -rf /work/gcc-x32-final && mkdir -p /work/gcc-x32-final && cd /work/gcc-x32-final
tar --no-same-owner -xf /src/gcc-15.2.0.tar.xz --strip-components=1
export PATH=/opt/x32/bin:$PATH
mkdir build && cd build
../configure --target=x86_64-linux-gnux32 --prefix=/opt/x32 --with-sysroot=/opt/x32 \
	--with-gmp=/usr --with-mpfr=/usr --with-mpc=/usr --with-isl=/usr \
	--enable-languages=c,c++ --disable-nls --disable-multilib --disable-bootstrap \
	--disable-libsanitizer --disable-libvtv --disable-libgomp \
	CFLAGS_FOR_TARGET="-Os -pipe -mx32" CXXFLAGS_FOR_TARGET="-Os -pipe -mx32"
make
make install
