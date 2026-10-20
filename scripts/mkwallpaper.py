#!/usr/bin/env python3
import os, re, struct, sys, tomllib, zlib

svg, out = sys.argv[1], sys.argv[2]
SCALE = 16
BG = (0x1a, 0x1b, 0x26)
text = open(svg).read()
with open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "distro.toml"), "rb") as f:
    HAT = tomllib.load(f)["hat_color"]
size = int(re.search(r'viewBox="0 0 (\d+) \d+"', text).group(1))
pixels = [[BG] * (size * SCALE) for _ in range(size * SCALE)]


def color(c):
    return tuple(int(c[i:i + 2], 16) for i in (1, 3, 5))


fill = None
for tag in re.finditer(r"<(/?)(g|rect)([^>]*)>", text):
    closing, name, attrs = tag.groups()
    if name == "g":
        fill = None if closing else HAT
        continue
    a = dict(re.findall(r'(\w+)="([^"]*)"', attrs))
    c = color(a.get("fill") or fill)
    x, y, w, h = (int(a[k]) for k in ("x", "y", "width", "height"))
    for py in range(y * SCALE, (y + h) * SCALE):
        for px in range(x * SCALE, (x + w) * SCALE):
            pixels[py][px] = c

raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in pixels)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


with open(out, "wb") as f:
    f.write(b"\x89PNG\r\n\x1a\n")
    f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", size * SCALE, size * SCALE, 8, 2, 0, 0, 0)))
    f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
    f.write(chunk(b"IEND", b""))
