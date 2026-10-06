#!/bin/sh
set -eu
umask 022
rm -rf /work/cc65 /out/cc65 && mkdir -p /work/cc65 && cd /work/cc65
tar --no-same-owner -xf /src/cc65-71746c829e77f74c2b601a7d16821170c2610df3.tar.gz --strip-components=1
make -j"$(nproc)" PREFIX=/usr bin
make -j"$(nproc)" PREFIX=/usr lib
make install PREFIX=/usr DESTDIR=/out/cc65
rm -rf /out/cc65/usr/share/doc /out/cc65/usr/share/cc65/samples
keep='c64 c128 vic20 plus4 cx16 nes pce atari apple2 apple2enh none sim6502 sim65c02'
for f in /out/cc65/usr/share/cc65/lib/*.lib /out/cc65/usr/share/cc65/target/*; do
	t=${f##*/}
	case " $keep " in *" ${t%.lib} "*) ;; *) rm -rf "$f" ;; esac
done
strip --strip-unneeded /out/cc65/usr/bin/*
