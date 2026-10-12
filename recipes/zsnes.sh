#!/bin/sh
set -eu
umask 022
CFLAGS="$CFLAGS -O3"
CXXFLAGS="$CXXFLAGS -O3"
export CFLAGS CXXFLAGS
rm -rf /work/zsnes && mkdir -p /work/zsnes && cd /work/zsnes
tar --no-same-owner -xf /src/zsnes-2.3.6.tar.gz --strip-components=1
make all OPT_FLAGS="$CFLAGS" WITH_AO= PREFIX=/usr
make install DESTDIR=/out/zsnes OPT_FLAGS="$CFLAGS" WITH_AO= PREFIX=/usr
mv /out/zsnes/usr/bin/zsnes /out/zsnes/usr/bin/zsnes-bin
printf '#!/bin/sh\nexec tinyhat-gpu /usr/bin/zsnes-bin "$@"\n' >/out/zsnes/usr/bin/zsnes
chmod 755 /out/zsnes/usr/bin/zsnes
