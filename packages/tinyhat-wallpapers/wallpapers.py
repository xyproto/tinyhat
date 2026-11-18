#!/usr/bin/env python3
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hat import LOW, hat, png

BG = (0x1a, 0x1b, 0x26)
GRAY = (0x49, 0x49, 0x49)
LIGHT = (0xc0, 0xca, 0xf5)
CYAN = (0x7d, 0xcf, 0xff)
TEAL = (0x1a, 0x9e, 0x96)
TWILL = (0x41, 0x48, 0x68)


def color(c):
    return tuple(int(c[i:i + 2], 16) for i in (1, 3, 5))


HAT = color(os.environ["TINYHAT_THEME_HAT_COLOR"])
HIGHLIGHT = color(os.environ["TINYHAT_THEME_HAT_HIGHLIGHT_COLOR"])


def logo(svg, out, scale=16):
    text = open(svg).read()
    size = int(re.search(r'viewBox="0 0 (\d+) \d+"', text).group(1))
    pixels = [[BG] * (size * scale) for _ in range(size * scale)]
    fill = None
    for tag in re.finditer(r"<(/?)(g|rect)([^>]*)>", text):
        closing, name, attrs = tag.groups()
        if name == "g":
            fill = None if closing else HAT
            continue
        a = dict(re.findall(r'(\w+)="([^"]*)"', attrs))
        c = color(a["fill"]) if a.get("fill") else fill
        x, y, w, h = (int(a[k]) for k in ("x", "y", "width", "height"))
        for py in range(y * scale, (y + h) * scale):
            for px in range(x * scale, (x + w) * scale):
                pixels[py][px] = c
    png(out, size * scale, size * scale, pixels)


def wirehat(out, scale=8):
    png(out, LOW * scale, LOW * scale, hat(0.45, [BG, HAT, HIGHLIGHT, GRAY, LIGHT, CYAN, TEAL], scale))


def houndstooth(out, scale=4):
    def dark(x, y):
        x, y = x % 8, y % 8
        return x < 4 if (x - y) % 4 < 2 else y < 4

    size = 8 * scale
    png(out, size, size, [[BG if dark(x // scale, y // scale) else TWILL for x in range(size)] for y in range(size)])


svg, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)
logo(svg, os.path.join(outdir, "tinyhat.png"))
wirehat(os.path.join(outdir, "wirehat.png"))
houndstooth(os.path.join(outdir, "hundetann.png"))
