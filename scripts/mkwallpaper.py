#!/usr/bin/env python3
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkgrubtheme import BG, HAT, png

svg, out = sys.argv[1], sys.argv[2]
SCALE = 16
text = open(svg).read()
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
    c = color(a["fill"]) if a.get("fill") else fill
    x, y, w, h = (int(a[k]) for k in ("x", "y", "width", "height"))
    for py in range(y * SCALE, (y + h) * SCALE):
        for px in range(x * SCALE, (x + w) * SCALE):
            pixels[py][px] = c

png(out, size * SCALE, size * SCALE, pixels)
