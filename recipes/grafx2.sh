#!/bin/sh
set -eu
umask 022
rm -rf /work/grafx2 /out/grafx2 && mkdir -p /work/grafx2/lua && cd /work/grafx2
tar --no-same-owner -xf /src/grafx2-f84cb09dc59d706d6e7b28778b01ba911f52298c.tar.gz --strip-components=1
tar --no-same-owner -xf /src/lua-5.4.9.tar.gz -C lua --strip-components=1
make -C lua/src liblua.a MYCFLAGS="$CFLAGS -DLUA_USE_LINUX"
mkdir -p 3rdparty/archives
cp /src/recoil-6.4.5.tar.gz /src/6502-v0.1.tar.xz 3rdparty/archives/
printf '#!/bin/sh\nopt=$1\nshift\nexec tar --no-same-owner -$opt "$@"\n' >/work/grafx2/tar
chmod 755 /work/grafx2/tar
patch -Np1 -i /patches/grafx2/grafx2-sdl3.patch
cd src
set -- API=sdl3 NO_X11=1 OPTIM=2 LUACOPT=-I/work/grafx2/lua/src LUALOPT=/work/grafx2/lua/src/liblua.a \
	TTFCOPT="$(pkg-config --cflags sdl3-ttf)" TTFLOPT="$(pkg-config --libs sdl3-ttf fontconfig)" \
	TAR=/work/grafx2/tar
make "$@"
make "$@" PREFIX=/usr DESTDIR=/out/grafx2 install
ln -sf grafx2-sdl3 /out/grafx2/usr/bin/grafx2
