#!/bin/sh
set -eu
umask 022
rm -rf /work/grub-efi64 /out/grub-efi64 && mkdir -p /work/grub-efi64 && cd /work/grub-efi64
tar --no-same-owner -xf /src/grub-2.12.tar.xz --strip-components=1
echo "depends bli part_gpt" >grub-core/extra_deps.lst
mkdir -p /work/grub-efi64/flex && bsdtar -xf /src/flex-2.6.4-5.2-i686.pkg.tar.zst -C /work/grub-efi64/flex usr/bin usr/lib usr/include
export PATH="/work/grub-efi64/flex/usr/bin:/opt/x32/bin:$PATH" LIBRARY_PATH=/work/grub-efi64/flex/usr/lib
unset CFLAGS CXXFLAGS LDFLAGS
./configure --prefix=/usr --with-platform=efi --target=x86_64 \
	TARGET_CC=x86_64-linux-gnux32-gcc TARGET_OBJCOPY=x86_64-linux-gnux32-objcopy TARGET_STRIP=x86_64-linux-gnux32-strip \
	TARGET_NM=x86_64-linux-gnux32-nm TARGET_RANLIB=x86_64-linux-gnux32-ranlib \
	--disable-werror --disable-nls --disable-efiemu --disable-grub-mkfont --disable-grub-themes \
	--disable-grub-mount --disable-device-mapper --disable-libzfs --disable-liblzma
make
make DESTDIR=/out/grub-efi64 install
