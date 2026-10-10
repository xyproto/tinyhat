#!/bin/sh
set -eu
umask 022
rm -rf /work/qemacs /out/qemacs && mkdir -p /work/qemacs && cd /work/qemacs
tar --no-same-owner -xf /src/qemacs-befd7a816ec824e36bccf3d91c10559bf6752400.tar.gz --strip-components=1
./configure --prefix=/usr --cc=gcc --disable-x11 --disable-xv --disable-xshm --disable-xrender --disable-html --disable-png --disable-ffmpeg --disable-plugins
make -j"$(nproc)" HOST_CC=gcc
make DESTDIR=/out/qemacs HOST_CC=gcc install
rm -rf /out/qemacs/usr/share/doc
mkdir -p /out/qemacs/usr/share/man
mv /out/qemacs/usr/man/man1 /out/qemacs/usr/share/man/
rmdir /out/qemacs/usr/man
[ -e /out/qemacs/usr/bin/qe ] || ln -s qemacs /out/qemacs/usr/bin/qe
strip --strip-unneeded /out/qemacs/usr/bin/qemacs
