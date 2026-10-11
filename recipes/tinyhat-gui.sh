#!/bin/sh
set -eu
umask 022
rm -rf /work/tinyhat-gui && mkdir -p /work/tinyhat-gui /out/tinyhat-gui/usr/bin
cd /work/tinyhat-gui
sed -i 's/>= *[0-9][0-9.]*//g' \
	/usr/lib/pkgconfig/pango.pc /usr/lib/pkgconfig/pangocairo.pc /usr/lib/pkgconfig/pangoft2.pc /usr/lib/pkgconfig/pangoxft.pc
for p in tinyhat-backup tinyhat-restore tinyhat-pick tinyhat-play; do
	cc $CFLAGS $(pkg-config --cflags gtk+-3.0 libcurl) -o /out/tinyhat-gui/usr/bin/$p /programs/$p.c $LDFLAGS $(pkg-config --libs gtk+-3.0 libcurl)
done
