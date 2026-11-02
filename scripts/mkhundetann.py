#!/usr/bin/env python3
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkgrubtheme

SCALE = 4
DARK = mkgrubtheme.BG
LIGHT = (0x41, 0x48, 0x68)

# Houndstooth is a 2/2 twill with 4 dark and 4 light threads in both directions:
# at each crossing the warp thread shows when (x - y) mod 4 < 2, and the weft thread otherwise.
def color(x, y):
    x, y = x % 8, y % 8
    dark = x < 4 if (x - y) % 4 < 2 else y < 4
    return DARK if dark else LIGHT


size = 8 * SCALE
rows = [[color(x // SCALE, y // SCALE) for x in range(size)] for y in range(size)]
mkgrubtheme.png(sys.argv[1], size, size, rows)
