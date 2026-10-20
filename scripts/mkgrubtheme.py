#!/usr/bin/env python3
import gzip, math, os, random, struct, sys, tomllib, zlib

BG = (0x1a, 0x1b, 0x26)
TEAL = (0x1a, 0x9e, 0x96)
with open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "distro.toml"), "rb") as f:
    DISTRO = tomllib.load(f)
HAT = tuple(int(DISTRO["hat_color"][i:i + 2], 16) for i in (1, 3, 5))
HIGHLIGHT = tuple(int(DISTRO["hat_highlight_color"][i:i + 2], 16) for i in (1, 3, 5))
GRAY = (0x49, 0x49, 0x49)
LIGHT = (0xc0, 0xca, 0xf5)
SLATE = (0xa9, 0xb1, 0xd6)

CYAN = (0x7d, 0xcf, 0xff)
WHITE = (0xff, 0xff, 0xff)

INK = [BG, HAT, HIGHLIGHT, GRAY, LIGHT, CYAN, TEAL]
LOW, SEGMENTS, HAT_SCALE = 64, 16, 2
ITEM, SPACING, BAR = 32, 2, 28


def png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


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


def project(angle, tilt, x, y, z):
    x1, z1 = x * math.cos(angle) + z * math.sin(angle), -x * math.sin(angle) + z * math.cos(angle)
    y1 = y - 0.45
    y2, z2 = y1 * math.cos(tilt) - z1 * math.sin(tilt), y1 * math.sin(tilt) + z1 * math.cos(tilt)
    f = 3.4 / (z2 + 3.4)
    return round(LOW / 2 + x1 * f * 25), round(LOW / 2 - y2 * f * 25), z2


def hat(angle):
    low = [[0] * LOW for _ in range(LOW)]
    tilt = 0.36 + 0.1 * math.sin(2 * angle)

    def line(a, b, c):
        x0, y0, x1, y1 = a[0], a[1], b[0], b[1]
        dx, sx, dy, sy = abs(x1 - x0), 1 if x0 < x1 else -1, -abs(y1 - y0), 1 if y0 < y1 else -1
        e = dx + dy
        while True:
            if 0 <= x0 < LOW and 0 <= y0 < LOW:
                low[y0][x0] = c
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * e
            if e2 >= dy:
                e, x0 = e + dy, x0 + sx
            if e2 <= dx:
                e, y0 = e + dx, y0 + sy

    def p(x, y, z):
        return project(angle, tilt, x, y, z)

    def at(r, y, a, t):
        return p(r * math.cos(a) - t * math.sin(a), y, r * math.sin(a) + t * math.cos(a))

    def edge(front_pass, a, b, front, rear):
        is_front = a[2] + b[2] < 0
        if is_front == front_pass:
            line(a, b, front if is_front else rear)

    def ring(front_pass, y, r, front, rear):
        for i in range(SEGMENTS):
            a0, a1 = 2 * math.pi * i / SEGMENTS, 2 * math.pi * (i + 1) / SEGMENTS
            edge(front_pass, p(r * math.cos(a0), y, r * math.sin(a0)), p(r * math.cos(a1), y, r * math.sin(a1)), front, rear)

    def quill(t, spread):
        w = spread * math.sin(math.pi * t)
        return at(0.58 + 0.45 * t - 0.15 * t * t + (0.04 if w else 0), 0.2 + 0.95 * t - (0.08 if w else 0), -0.45 - 0.35 * t, w)

    for front_pass in (False, True):
        for side in (-1, 1):
            knot, top, bottom = at(0.56, 0.19, 0, 0), at(0.6, 0.27, 0, side * 0.15), at(0.6, 0.11, 0, side * 0.15)
            edge(front_pass, knot, top, 4, 3)
            edge(front_pass, top, bottom, 4, 3)
            edge(front_pass, bottom, knot, 4, 3)
        ring(front_pass, 0, 1.0, 2, 1)
        ring(front_pass, 0, 0.55, 2, 1)
        ring(front_pass, 0.12, 0.555, 4, 3)
        ring(front_pass, 0.26, 0.56, 4, 3)
        ring(front_pass, 0.95, 0.6, 2, 1)
        for i in range(0, SEGMENTS, 2):
            a = 2 * math.pi * i / SEGMENTS
            edge(front_pass, p(0.55 * math.cos(a), 0, 0.55 * math.sin(a)), p(0.6 * math.cos(a), 0.95, 0.6 * math.sin(a)), 2, 1)
            edge(front_pass, p(0.55 * math.cos(a), 0, 0.55 * math.sin(a)), p(math.cos(a), 0, math.sin(a)), 2, 1)
        for k in range(6):
            edge(front_pass, quill(k / 6, 0), quill((k + 1) / 6, 0), 5, 6)
        for k in range(1, 6):
            for side in (-1, 1):
                edge(front_pass, quill(k / 6, 0), quill(k / 6, side * 0.17), 5, 6)
    return [[INK[low[y // HAT_SCALE][x // HAT_SCALE]] for x in range(LOW * HAT_SCALE)] for y in range(LOW * HAT_SCALE)]


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


def bdf(path, glyphs, scale):
    w, h, descent = 8 * scale, 16 * scale, 4 * scale
    with open(path, "w") as f:
        f.write(f"STARTFONT 2.1\nFONT -tinyhat-vga-medium-r-normal--{h}-{h * 10}-75-75-c-{w * 10}-iso10646-1\n")
        f.write(f"SIZE {h} 75 75\nFONTBOUNDINGBOX {w} {h} 0 -{descent}\n")
        f.write(f'STARTPROPERTIES 5\nFAMILY_NAME "VGA"\nWEIGHT_NAME "Medium"\nPIXEL_SIZE {h}\n')
        f.write(f"FONT_ASCENT {h - descent}\nFONT_DESCENT {descent}\nENDPROPERTIES\n")
        f.write(f"CHARS {0x7f - 0x20}\n")
        for code in range(0x20, 0x7f):
            f.write(f"STARTCHAR U+{code:04X}\nENCODING {code}\nSWIDTH 500 0\nDWIDTH {w} 0\nBBX {w} {h} 0 -{descent}\nBITMAP\n")
            for row in glyphs[code * 16:code * 16 + 16]:
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


out, psf = sys.argv[1], sys.argv[2]
data = gzip.open(psf).read()
magic, _, header, _, count, glyph, height, width = struct.unpack("<8I", data[:32])
if magic != 0x864AB572 or count < 256 or glyph != 16 or height != 16 or width != 8:
    sys.exit(f"{psf}: expected an 8x16 PSF2 font")
glyphs = data[header:header + 256 * 16]
os.makedirs(out, exist_ok=True)
png(os.path.join(out, "hat.png"), LOW * HAT_SCALE, LOW * HAT_SCALE, hat(0.45))
starfield(os.path.join(out, "stars.png"))
slices(out, "select", ITEM, TEAL)
slices(out, "bar", BAR, GRAY)
slices(out, "fill", BAR, TEAL)
bdf(os.path.join(out, "vga-16.bdf"), glyphs, 1)
bdf(os.path.join(out, "vga-32.bdf"), glyphs, 2)
with open(os.path.join(out, "theme.txt"), "w") as f:
    f.write(theme())
with open(os.path.join(out, "theme.cfg"), "w") as f:
    f.write("if loadfont $theme_dir/vga-16.pf2 $theme_dir/vga-32.pf2; then\n")
    f.write(f'  set gfxterm_font="{SMALL}"\n')
    f.write("  set gfxmode=auto\n")
    f.write("  if terminal_output gfxterm; then\n")
    f.write("    set theme=$theme_dir/theme.txt\n")
    f.write("  fi\n")
    f.write("fi\n")
