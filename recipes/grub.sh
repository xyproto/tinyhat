#!/bin/sh
set -eu
umask 022
rm -rf /work/grub /out/grub && mkdir -p /work/grub && cd /work/grub
tar --no-same-owner -xf /src/grub-2.12.tar.xz --strip-components=1
patch -Np1 -i /patches/grub/mode13h.patch
echo "depends bli part_gpt" >grub-core/extra_deps.lst
mkdir -p /work/grub/flex && bsdtar -xf /src/flex-2.6.4-5.2-i686.pkg.tar.zst -C /work/grub/flex usr/bin usr/lib usr/include
export PATH="/work/grub/flex/usr/bin:$PATH" LIBRARY_PATH=/work/grub/flex/usr/lib CPATH=/work/grub/flex/usr/include
unset CFLAGS CXXFLAGS LDFLAGS
./configure --prefix=/usr --sysconfdir=/etc --with-platform=pc --target=i386 \
	--disable-werror --disable-nls --disable-efiemu --disable-grub-mkfont --disable-grub-themes \
	--disable-grub-mount --disable-device-mapper --disable-libzfs --disable-liblzma
make
make DESTDIR=/out/grub install
