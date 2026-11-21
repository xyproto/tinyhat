#!/usr/bin/env python3
import gzip, os, random, struct, sys, tomllib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "packages", "tinyhat-wallpapers"))
from hat import HAT_SCALE, LOW, SEGMENTS, png, project
from hat import hat as hatpixels

BG = (0x1a, 0x1b, 0x26)
TEAL = (0x1a, 0x9e, 0x96)
with open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "theme.toml"), "rb") as f:
    DISTRO = tomllib.load(f)
HAT = tuple(int(DISTRO["hat_color"][i:i + 2], 16) for i in (1, 3, 5))
HIGHLIGHT = tuple(int(DISTRO["hat_highlight_color"][i:i + 2], 16) for i in (1, 3, 5))
GRAY = (0x49, 0x49, 0x49)
LIGHT = (0xc0, 0xca, 0xf5)
SLATE = (0xa9, 0xb1, 0xd6)

CYAN = (0x7d, 0xcf, 0xff)
WHITE = (0xff, 0xff, 0xff)

INK = [BG, HAT, HIGHLIGHT, GRAY, LIGHT, CYAN, TEAL]
ITEM, SPACING, BAR = 32, 2, 28


def pill(x0, x1, h, left, right):
    r = h / 2

    def inside(x, y):
        if not (x0 <= x < x1 and 0 <= y < h):
            return False
        if left and x < x0 + r:
            return (x - x0 - r) ** 2 + (y - r) ** 2 <= r * r
        if right and x >= x1 - r:
            return (x - x1 + r) ** 2 + (y - r) ** 2 <= r * r
        return True

    return inside


def render(w, h, inside, color):
    sub = [(i + 0.5) / 4 for i in range(4)]
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            n = sum(inside(x + sx, y + sy) for sx in sub for sy in sub)
            row.append(tuple(round(b + (c - b) * n / 16) for b, c in zip(BG, color)))
        rows.append(row)
    return rows


def slices(out, prefix, h, color):
    r = h // 2
    png(os.path.join(out, f"{prefix}_w.png"), r, h, render(r, h, pill(0, 2 * r, h, True, False), color))
    png(os.path.join(out, f"{prefix}_c.png"), 1, h, [[color]] * h)
    png(os.path.join(out, f"{prefix}_e.png"), r, h, render(r, h, pill(-r, r, h, False, True), color))


def hat(angle, scale=HAT_SCALE):
    return hatpixels(angle, INK, scale)


def starfield(path, w=1280, h=800):
    rnd = random.Random(0x7417A700)
    size = LOW * HAT_SCALE
    keep_out = [
        (w // 2 - size // 2 - 24, h * 5 // 100 - 24, w // 2 + size // 2 + 24, h * 5 // 100 + size + 48),
        (w * 8 // 100, h * 38 // 100, w * 92 // 100, h * 88 // 100),
        (w * 28 // 100, h * 90 // 100 - 32, w * 72 // 100, h * 90 // 100 + BAR + 8),
        (w * 68 // 100, 0, w, h * 2 // 100 + 28),
    ]
    shades = [(0x56, 0x5f, 0x89)] * 5 + [(0xa9, 0xb1, 0xd6)] * 3 + [WHITE]
    rows = [[BG] * w for _ in range(h)]
    n = 0
    while n < 140:
        x, y = rnd.randrange(w - 1), rnd.randrange(h - 1)
        if any(a <= x < c and b <= y < d for a, b, c, d in keep_out):
            continue
        c = rnd.choice(shades)
        for py in (y, y + 1):
            for px in (x, x + 1):
                rows[py][px] = c
        n += 1
    png(path, w, h, rows)


def bdf(path, glyphs, codes, scale):
    w, h, descent = 8 * scale, 16 * scale, 4 * scale
    with open(path, "w") as f:
        f.write(f"STARTFONT 2.1\nFONT -tinyhat-vga-medium-r-normal--{h}-{h * 10}-75-75-c-{w * 10}-iso10646-1\n")
        f.write(f"SIZE {h} 75 75\nFONTBOUNDINGBOX {w} {h} 0 -{descent}\n")
        f.write(f'STARTPROPERTIES 5\nFAMILY_NAME "VGA"\nWEIGHT_NAME "Medium"\nPIXEL_SIZE {h}\n')
        f.write(f"FONT_ASCENT {h - descent}\nFONT_DESCENT {descent}\nENDPROPERTIES\n")
        f.write(f"CHARS {len(codes)}\n")
        for code, index in sorted(codes.items()):
            f.write(f"STARTCHAR U+{code:04X}\nENCODING {code}\nSWIDTH 500 0\nDWIDTH {w} 0\nBBX {w} {h} 0 -{descent}\nBITMAP\n")
            for row in glyphs[index * 16:index * 16 + 16]:
                bits = 0
                for i in range(8):
                    if row & (0x80 >> i):
                        bits |= ((1 << scale) - 1) << ((7 - i) * scale)
                f.write(f"{bits:0{w // 4}X}\n" * scale)
            f.write("ENDCHAR\n")
        f.write("ENDFONT\n")


def hexc(c):
    return "#%02x%02x%02x" % c


def label(x, y, w, h, text, font, color, align="center", ident=None):
    t = "+ label {\n"
    if ident:
        t += f'\tid = "{ident}"\n'
    return t + (
        f'\tleft = {x}\n\ttop = {y}\n\twidth = {w}\n\theight = {h}\n'
        f'\ttext = "{text}"\n\tfont = "{font}"\n\tcolor = "{hexc(color)}"\n\talign = "{align}"\n'
        "}\n"
    )


SMALL, LARGE = "VGA Regular 16", "VGA Regular 32"


def theme():
    size = LOW * HAT_SCALE
    return f"""desktop-color: "{hexc(BG)}"
desktop-image: "stars.png"
desktop-image-scale-method: "crop"
title-text: ""
terminal-font: "{SMALL}"
terminal-left: "0"
terminal-top: "0"
terminal-width: "100%"
terminal-height: "100%"
terminal-border: "0"

+ image {{
	left = 50%-{size // 2}
	top = 5%
	width = {size}
	height = {size}
	file = "hat.png"
}}
+ boot_menu {{
	left = 10%
	top = 40%
	width = 80%
	height = 47%
	item_font = "{LARGE}"
	selected_item_font = "{LARGE}"
	item_color = "{hexc(LIGHT)}"
	selected_item_color = "{hexc(BG)}"
	item_height = {ITEM}
	item_padding = 8
	item_spacing = {SPACING}
	icon_width = 0
	icon_height = 0
	item_icon_space = 0
	selected_item_pixmap_style = "select_*.png"
	scrollbar = false
}}
+ progress_bar {{
	id = "__timeout__"
	left = 30%
	top = 90%
	width = 40%
	height = {BAR}
	text = ""
	bar_style = "bar_*.png"
	highlight_style = "fill_*.png"
	highlight_overlay = true
}}
""" + label("0", f"5%+{size + 4}", "100%", 36, "TINY HAT", LARGE, WHITE) + label(
        "0", "90%-24", "100%", 20, "BOOTING IN %d", SMALL, LIGHT, ident="__timeout__"
    ) + label("70%", "2%", "28%", 20, "E EDIT  C CONSOLE", SMALL, SLATE, "right")


def main():
    out, psf = sys.argv[1], sys.argv[2]
    data = gzip.open(psf).read()
    magic, _, header, flags, count, glyph, height, width = struct.unpack("<8I", data[:32])
    if magic != 0x864AB572 or count < 256 or glyph != 16 or height != 16 or width != 8:
        sys.exit(f"{psf}: expected an 8x16 PSF2 font")
    glyphs = data[header:header + count * 16]
    codes = {code: code for code in range(0x20, 0x7f)}
    if flags & 1:
        table = data[header + count * 16:]
        for index, entry in enumerate(table.split(b"\xff")[:count]):
            for ch in entry.split(b"\xfe")[0].decode("utf-8", "replace"):
                c = ord(ch)
                if 0x2500 <= c <= 0x257f or 0x2190 <= c <= 0x2193 or c in (0x25b2, 0x25bc, 0x25ba, 0x25c4):
                    codes.setdefault(c, index)
    os.makedirs(out, exist_ok=True)
    png(os.path.join(out, "hat.png"), LOW * HAT_SCALE, LOW * HAT_SCALE, hat(0.45))
    starfield(os.path.join(out, "stars.png"))
    slices(out, "select", ITEM, TEAL)
    slices(out, "bar", BAR, GRAY)
    slices(out, "fill", BAR, TEAL)
    bdf(os.path.join(out, "vga-16.bdf"), glyphs, codes, 1)
    bdf(os.path.join(out, "vga-32.bdf"), glyphs, codes, 2)
    with open(os.path.join(out, "theme.txt"), "w") as f:
        f.write(theme())
    with open(os.path.join(out, "theme.cfg"), "w") as f:
        f.write("if loadfont $theme_dir/vga-16.pf2 $theme_dir/vga-32.pf2; then\n")
        f.write(f'  set gfxterm_font="{SMALL}"\n')
        f.write("  set gfxmode=auto\n")
        f.write("  if terminal_output gfxterm; then\n")
        f.write("    set theme=$theme_dir/theme.txt\n")
        f.write("    export theme\n")
        f.write("  fi\n")
        f.write("fi\n")


if __name__ == "__main__":
    main()
