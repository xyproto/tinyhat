#!/usr/bin/env python3
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkgrubtheme

SCALE = 4
DARK = mkgrubtheme.BG
LIGHT = (0x41, 0x48, 0x68)


def color(x, y):
    warp = x % 8 < 4
    weft = y % 8 < 4
    return LIGHT if (warp if (x + y) % 4 < 2 else weft) else DARK


size = 8 * SCALE
rows = [[color(x // SCALE, y // SCALE) for x in range(size)] for y in range(size)]
mkgrubtheme.png(sys.argv[1], size, size, rows)
