#!/bin/sh
set -eu
umask 022
. /recipes/x32-env.sh
out=/out/x32-runtime
rm -rf "$out" && mkdir -p "$out/usr/libx32"
ln -s usr/libx32 "$out/libx32"
for f in ld-linux-x32.so.2 libc.so.6 libm.so.6 libmvec.so.1 libpthread.so.0 libdl.so.2 librt.so.1 libresolv.so.2 \
	libanl.so.1 libutil.so.1 libnss_files.so.2 libnss_dns.so.2; do
	cp -L "/opt/x32/libx32/$f" "$out/usr/libx32/$f"
done
cp -a /opt/x32/x86_64-linux-gnux32/libx32/libgcc_s.so.1 /opt/x32/x86_64-linux-gnux32/libx32/libatomic.so.1* \
	/opt/x32/x86_64-linux-gnux32/libx32/libstdc++.so.6 /opt/x32/x86_64-linux-gnux32/libx32/libstdc++.so.6.0.* "$out/usr/libx32/"
rm -f "$out"/usr/libx32/*-gdb.py
find "$out/usr/libx32" -type f ! -name 'ld-linux-x32.so.2' -exec "$STRIP" --strip-unneeded {} +
