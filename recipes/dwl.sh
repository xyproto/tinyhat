#!/bin/sh
set -eu
umask 022
rm -rf /work/dwl && mkdir -p /work/dwl && cd /work/dwl
tar --no-same-owner -xf /src/dwl-0.8.tar.gz --strip-components=1
patch -Np1 -i /patches/dwl/bar.patch
patch -Np1 -i /patches/dwl/borders.patch
patch -Np1 -i /patches/dwl/notice.patch
patch -Np1 -i /patches/dwl/gaps.patch
patch -Np1 -i /patches/dwl/keys.patch
patch -Np1 -i /patches/dwl/filter.patch
patch -Np1 -i /patches/dwl/killclient.patch
patch -Np1 -i /patches/dwl/floatsize.patch
patch -Np1 -i /patches/dwl/trim.patch
patch -Np1 -i /patches/dwl/center.patch
patch -Np1 -i /patches/dwl/menubutton.patch
patch -Np1 -i /patches/dwl/barline.patch
cp /patches/dwl/config.h config.h
make PREFIX=/usr CFLAGS="$CFLAGS -DWLR_USE_UNSTABLE -std=c11"
make PREFIX=/usr DESTDIR=/out/dwl install
