#!/bin/sh
set -eu
umask 022
rm -rf /work/mgba /out/mgba && mkdir -p /work/mgba && cd /work/mgba
tar --no-same-owner -xf /src/mgba-0.10.5.tar.gz --strip-components=1
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
	-DBUILD_QT=OFF -DBUILD_SDL=ON -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DUSE_EPOXY=OFF \
	-DBUILD_SHARED=OFF -DBUILD_STATIC=ON -DUSE_FFMPEG=OFF -DUSE_MINIZIP=OFF -DUSE_LIBZIP=OFF \
	-DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_LUA=OFF -DENABLE_SCRIPTING=OFF -DUSE_LZMA=OFF \
	-DUSE_DISCORD_RPC=OFF -DUSE_EDITLINE=OFF -DUSE_GDB_STUB=OFF -DUSE_DEBUGGERS=OFF
ninja -C b
DESTDIR=/out/mgba ninja -C b install
rm -rf /out/mgba/usr/include /out/mgba/usr/lib /out/mgba/usr/share/doc /out/mgba/usr/share/applications /out/mgba/usr/share/icons
