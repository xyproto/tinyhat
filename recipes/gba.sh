#!/bin/sh
set -eu
umask 022
rm -rf /work/gba /out/gba && mkdir -p /work/gba/tonc /work/gba/tools && cd /work/gba
tar --no-same-owner -xf /src/libtonc-c5af1b2cb019dcde43216596390490bc07800b21.tar.gz -C tonc --strip-components=1
tar --no-same-owner -xf /src/gba-tools-v1.2.0.tar.gz -C tools --strip-components=1
lib=/out/gba/usr/arm-none-eabi/lib
mkdir -p "$lib/thumb/nofp" /out/gba/usr/arm-none-eabi/include /out/gba/usr/bin
sh /programs/gba/build-libtonc.sh tonc "$lib/libtonc.a"
for d in "$lib" "$lib/thumb/nofp"; do
	arm-none-eabi-gcc -mcpu=arm7tdmi -marm -c -o "$d/gba_crt0.o" /programs/gba/gba_crt0.s
	install -m644 /programs/gba/gba_cart.ld /programs/gba/gba.specs "$d/"
done
install -m644 tonc/include/*.h /out/gba/usr/arm-none-eabi/include/
gcc $CFLAGS -o /out/gba/usr/bin/gbafix tools/src/gbafix.c $LDFLAGS
