#!/bin/sh
set -eu
umask 022
rm -rf /work/tic80 /out/tic80 && mkdir -p /work/tic80 /out/tic80/usr/bin /out/tic80/usr/lib/tic80 && cd /work/tic80
tar --no-same-owner -xf /src/tic80-1.3.1.tar.gz --strip-components=1
for m in argparse-0d5f5d07 blip-buf-330226d9 giflib-1aa11b06 jsmn-25647e69 libpng-ed217e3e lua-75ea9ccb naett-10a96244 zip-296ff242 zlib-51b7f2ab; do
	mkdir -p "vendor/${m%-*}"
	tar --no-same-owner -xf "/src/tic80-$m.tar.gz" --strip-components=1 -C "vendor/${m%-*}"
done
sed -i 's/if(BUILD_STATIC AND NOT USE_GLES2)/if(BUILD_STATIC AND NOT USE_GLES2 AND NOT PREFER_SYSTEM_SDL2)/' cmake/sdl.cmake
CFLAGS="$CFLAGS -fno-pie"
LDFLAGS="$LDFLAGS -no-pie"
export CFLAGS LDFLAGS
cmake -S . -B b -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON -DCMAKE_POSITION_INDEPENDENT_CODE=OFF \
	-DBUILD_WITH_ALL=OFF -DBUILD_WITH_LUA=ON -DBUILD_WITH_FENNEL=ON -DPREFER_SYSTEM_SDL2=ON \
	-DBUILD_SDLGPU=OFF -DBUILD_SURF=OFF -DBUILD_PLAYER=OFF -DBUILD_STUB=OFF
ninja -C b
install -m755 b/bin/tic80 /out/tic80/usr/lib/tic80/
strip --strip-unneeded /out/tic80/usr/lib/tic80/*
printf '#!/bin/sh\nexec tinyhat-gpu /usr/lib/tic80/tic80 "$@"\n' >/out/tic80/usr/bin/tic80
chmod 755 /out/tic80/usr/bin/tic80
