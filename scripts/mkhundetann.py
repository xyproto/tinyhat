#!/usr/bin/env python3
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkgrubtheme

SCALE = 4
DARK = mkgrubtheme.BG
LIGHT = (0x41, 0x48, 0x68)

# Houndstooth from 8x8.me by ace-dent (CC0): https://github.com/ace-dent/8x8.me/tree/main/11-Woven
# Weaving it as a 2/2 twill instead leaves one-thread nubs on the teeth.
TILE = [
    "00000110",
    "00001100",
    "00011000",
    "11011110",
    "10011111",
    "00111111",
    "01111110",
    "00010010",
]


def color(x, y):
    return LIGHT if TILE[y % 8][x % 8] == "1" else DARK


size = 8 * SCALE
rows = [[color(x // SCALE, y // SCALE) for x in range(size)] for y in range(size)]
mkgrubtheme.png(sys.argv[1], size, size, rows)
