#!/bin/sh
set -eu
umask 022
rm -rf /work/tinyhat-splash /out/tinyhat-splash && mkdir -p /work/tinyhat-splash /out/tinyhat-splash/usr/bin
cd /work/tinyhat-splash
python3 - /usr/share/kbd/consolefonts/default8x16.psfu.gz >font.h <<'PY'
import gzip, struct, sys
d = gzip.open(sys.argv[1]).read()
magic, _, size, _, count, glyph, height, width = struct.unpack("<8I", d[:32])
assert magic == 0x864AB572 and count >= 256 and glyph == 16 and height == 16 and width == 8
data = d[size:size + 256 * 16]
print("static const unsigned char font8x16[256 * 16] = {")
for i in range(0, len(data), 16):
    print("\t" + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
print("};")
PY
cc $CFLAGS -static -I. -o /out/tinyhat-splash/usr/bin/tinyhat-splash /programs/tinyhat-splash.c $LDFLAGS -lm
strip --strip-all /out/tinyhat-splash/usr/bin/tinyhat-splash
