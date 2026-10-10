#!/bin/sh
set -eu
umask 022
rm -rf /work/vice /out/vice && mkdir -p /work/vice && cd /work/vice
tar --no-same-owner -xf /src/vice-3.9.tar.gz --strip-components=1
mkdir -p /work/vice/flex && bsdtar -xf /src/flex-2.6.4-5.2-i686.pkg.tar.zst -C /work/vice/flex usr/bin usr/lib usr/include
bsdtar -xf /src/xa-2.4.1-2.0-i686.pkg.tar.zst -C /work/vice/flex usr/bin
printf '#!/bin/sh\nfor f; do sed -i "s/\\r$//" "$f"; done\n' >/work/vice/flex/usr/bin/dos2unix
chmod 755 /work/vice/flex/usr/bin/dos2unix
export PATH="/work/vice/flex/usr/bin:$PATH" LIBRARY_PATH=/work/vice/flex/usr/lib CPATH=/work/vice/flex/usr/include
./configure --prefix=/usr --enable-sdl2ui --with-sdlsound --without-alsa --without-pulse \
	--disable-html-docs --disable-pdf-docs --without-flac --without-vorbis --without-mpg123 --without-lame \
	--disable-ffmpeg --disable-ethernet --disable-catweasel --disable-hardsid --disable-parsid --disable-midi \
	SDL2_IMAGE_CFLAGS="-I/out/sdl2_image/usr/include/SDL2 $(pkg-config --cflags sdl2-compat)" SDL2_IMAGE_LIBS="-L/out/sdl2_image/usr/lib -lSDL2_image"
make -j"$(nproc)"
make DESTDIR=/out/vice install
cd /out/vice/usr
find bin -type f ! -name x64sc ! -name c1541 -delete
find bin -type l -delete
rm -rf share/doc share/man share/info share/applications share/icons share/metainfo
cd share/vice
rm -rf C128 C64DTV CBM-II PET PLUS4 SCPU64 VIC20 GLSL
rm -f C64/gtk3_*.vkm common/*.svg common/*.ttf common/icon1024x1024.png common/Icon-128@2x.png
find common -name '*.png' ! -name 'C64_*' -delete
strip --strip-unneeded /out/vice/usr/bin/x64sc /out/vice/usr/bin/c1541
