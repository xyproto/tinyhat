#!/bin/sh
set -eu
umask 022
rm -rf /work/gdb /out/gdb && mkdir -p /work/gdb/build && cd /work/gdb
tar --no-same-owner -xf /src/gdb-17.1.tar.xz --strip-components=1
cd build
../configure --prefix=/usr --disable-nls --disable-werror --disable-sim --disable-gdbserver --disable-gprofng \
	--disable-binutils --disable-ld --disable-gas --disable-gold --disable-gprof --disable-inprocess-agent \
	--without-python --without-guile --without-babeltrace --without-debuginfod --without-intel-pt \
	--disable-source-highlight --without-xxhash --with-system-readline --with-system-zlib --with-expat \
	MAKEINFO=true
make MAKEINFO=true all-gdb
make MAKEINFO=true DESTDIR=/out/gdb install-gdb
rm -rf /out/gdb/usr/share/info /out/gdb/usr/include /out/gdb/usr/lib
strip --strip-unneeded /out/gdb/usr/bin/gdb
