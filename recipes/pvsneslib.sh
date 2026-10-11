#!/bin/sh
set -eu
umask 022
rm -rf /work/pvsneslib /out/pvsneslib && mkdir -p /work/pvsneslib
cd /work/pvsneslib
tar --no-same-owner -xf /src/pvsneslib-e090f001ccffdc0fb69f479a8c9703e30d07f6b1.tar.gz --strip-components=1
tar --no-same-owner -xf /src/tcc-cc076f049282b7f8ca46b15c437db97d9224e763.tar.gz -C compiler/tcc --strip-components=1
tar --no-same-owner -xf /src/wla-dx-6fa1f673f010e4fa4571c40929019cd7e67d1bbd.tar.gz -C compiler/wla-dx --strip-components=1
CFLAGS="$CFLAGS -O3"
CXXFLAGS="$CXXFLAGS -O3"
export CFLAGS CXXFLAGS
export PVSNESLIB_HOME=/work/pvsneslib
make compiler tools pvsneslib
mkdir -p /out/pvsneslib/usr/share/pvsneslib/pvsneslib
cp -r devkitsnes /out/pvsneslib/usr/share/pvsneslib/
cp -r pvsneslib/include pvsneslib/lib /out/pvsneslib/usr/share/pvsneslib/pvsneslib/
for f in /out/pvsneslib/usr/share/pvsneslib/devkitsnes/bin/*; do
	strip --strip-unneeded "$f" 2>/dev/null || true
done
