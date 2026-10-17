#!/usr/bin/env python3
import os, struct, sys, zlib

W, H = 1920, 1200
BG = (0x1a, 0x1b, 0x26)
TEAL = (0x1a, 0x9e, 0x96)
ARCH = (0x17, 0x93, 0xd1)
HAT = (0x92, 0x24, 0x24)
PINK = (0xf7, 0x76, 0x8e)
GRAY = (0x49, 0x49, 0x49)
CYAN = (0x7d, 0xcf, 0xff)
LIGHT = (0xc0, 0xca, 0xf5)
SLATE = (0xa9, 0xb1, 0xd6)


def png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def rect(x0, y0, x1, y1):
    return (lambda x, y: x0 <= x < x1 and y0 <= y < y1), (x0, y0, x1, y1)


def pill(x0, y0, x1, y1, left=False, right=True):
    r = (y1 - y0) / 2
    cy = y0 + r

    def inside(x, y):
        if not (x0 <= x < x1 and y0 <= y < y1):
            return False
        if left and x < x0 + r:
            return (x - x0 - r) ** 2 + (y - cy) ** 2 <= r * r
        if right and x >= x1 - r:
            return (x - x1 + r) ** 2 + (y - cy) ** 2 <= r * r
        return True

    return inside, (x0, y0, x1, y1)


def elbow(x0, x1, ybar0, ybar1, yend, xend, outer, inner, top):
    def inside(x, y):
        if top:
            vert = x0 <= x < x1 and ybar0 <= y < yend
            cx, cy = x0 + outer, ybar0 + outer
            corner = x < cx and y < cy
            fx, fy = x1 + inner, ybar1 + inner
            fillet = x1 <= x < fx and ybar1 <= y < fy and (x - fx) ** 2 + (y - fy) ** 2 >= inner * inner
        else:
            vert = x0 <= x < x1 and yend <= y < ybar1
            cx, cy = x0 + outer, ybar1 - outer
            corner = x < cx and y >= cy
            fx, fy = x1 + inner, ybar0 - inner
            fillet = x1 <= x < fx and fy <= y < ybar0 and (x - fx) ** 2 + (y - fy) ** 2 >= inner * inner
        horiz = x0 <= x < xend and ybar0 <= y < ybar1
        if corner and (vert or horiz):
            return (x - cx) ** 2 + (y - cy) ** 2 <= outer * outer
        return vert or horiz or fillet

    if top:
        return inside, (x0, ybar0, max(xend, x1 + inner), yend)
    return inside, (x0, yend, max(xend, x1 + inner), ybar1)


def render(w, h, shapes, bg=BG):
    rows = [[bg] * w for _ in range(h)]
    sub = [(i + 0.5) / 4 for i in range(4)]
    for (inside, box), color in shapes:
        bx0, by0, bx1, by1 = (max(0, int(box[0])), max(0, int(box[1])), min(w, int(box[2]) + 1), min(h, int(box[3]) + 1))
        for y in range(by0, by1):
            row = rows[y]
            for x in range(bx0, bx1):
                corners = (inside(x, y), inside(x + 0.999, y), inside(x, y + 0.999), inside(x + 0.999, y + 0.999))
                if all(corners):
                    row[x] = color
                    continue
                n = sum(inside(x + sx, y + sy) for sx in sub for sy in sub)
                if n:
                    row[x] = tuple(round(b + (c - b) * n / 16) for b, c in zip(bg, color))
    return rows


def hexc(c):
    return "#%02x%02x%02x" % c


def label(x, y, w, h, text, font, color, align="left"):
    return (
        "+ label {\n"
        f'\tleft = {x}\n\ttop = {y}\n\twidth = {w}\n\theight = {h}\n'
        f'\ttext = "{text}"\n\tfont = "{font}"\n\tcolor = "{hexc(color)}"\n\talign = "{align}"\n'
        "}\n"
    )


def bold(size):
    return f"DejaVu Sans Bold {size}"


SMALL, MEDIUM, LARGE, HUGE, MONO = 16, 24, 32, 64, 16
ITEM, SPACING = 40, 12

SIDE = (2, 14)
TOP = (3, 8)
BOTTOM = (92, 97)
BLOCKS = [
    (23, 33, HAT, LIGHT, "11-386"),
    (34, 43, GRAY, LIGHT, "04-486"),
    (44, 56, CYAN, BG, "07-586"),
    (57, 64, PINK, BG, "09-686"),
]


def X(p):
    return round(p * W / 100)


def Y(p):
    return round(p * H / 100)


def background():
    r = min(W, H) / 100
    shapes = [
        (elbow(X(SIDE[0]), X(SIDE[1]), Y(TOP[0]), Y(TOP[1]), Y(22), X(58), 8 * r, 3 * r, True), TEAL),
        (rect(X(59), Y(TOP[0]), X(64), Y(TOP[1])), SLATE),
        (pill(X(92), Y(TOP[0]), X(98), Y(TOP[1])), PINK),
        (elbow(X(SIDE[0]), X(SIDE[1]), Y(BOTTOM[0]), Y(BOTTOM[1]), Y(65), X(58), 8 * r, 3 * r, False), ARCH),
        (rect(X(59), Y(BOTTOM[0]), X(61), Y(BOTTOM[1])), CYAN),
        (pill(X(91), Y(BOTTOM[0]), X(98), Y(BOTTOM[1])), HAT),
    ]
    shapes += [(rect(X(SIDE[0]), Y(y0), X(SIDE[1]), Y(y1)), color) for y0, y1, color, _, _ in BLOCKS]
    return render(W, H, shapes)


def theme():
    lh = SMALL + 8
    side_w = f"{SIDE[1] - SIDE[0] - 1}%"
    t = f"""desktop-image: "background.png"
desktop-image-scale-method: "stretch"
desktop-color: "{hexc(BG)}"
title-text: ""
terminal-font: "DejaVu Sans Mono Regular {MONO}"
terminal-left: "18%"
terminal-top: "12%"
terminal-width: "78%"
terminal-height: "76%"
terminal-border: "0"

+ boot_menu {{
	left = 18%
	top = 12%
	width = 78%
	height = 68%
	item_font = "{bold(MEDIUM)}"
	selected_item_font = "{bold(MEDIUM)}"
	item_color = "{hexc(LIGHT)}"
	selected_item_color = "{hexc(BG)}"
	item_height = {ITEM}
	item_padding = 4
	item_spacing = {SPACING}
	icon_width = 0
	icon_height = 0
	item_icon_space = 0
	selected_item_pixmap_style = "select_*.png"
	scrollbar = false
}}
+ progress_bar {{
	id = "__timeout__"
	left = 18%
	top = 84%
	width = 40%
	height = 28
	font = "{bold(SMALL)}"
	text = "AUTO BOOT IN %d SECONDS"
	text_color = "{hexc(LIGHT)}"
	fg_color = "{hexc(TEAL)}"
	bg_color = "{hexc(GRAY)}"
	border_color = "{hexc(BG)}"
}}
"""
    t += label("65%", f"{(TOP[0] + TOP[1]) // 2}%-{LARGE // 2 + 4}", "26%", LARGE + 8, "TINY HAT", bold(LARGE), PINK, "right")
    t += label("62%", f"{(BOTTOM[0] + BOTTOM[1]) // 2}%-{lh // 2}", "28%", lh, "E EDIT   C CONSOLE", bold(SMALL), CYAN, "center")
    t += label(f"{SIDE[0]}%", f"22%-{lh + 2}", side_w, lh, "BOOT-1", bold(SMALL), BG, "right")
    t += label(f"{SIDE[0]}%", "65%+2", side_w, lh, "32-BIT", bold(SMALL), BG, "right")
    for y0, y1, _, ink, text in BLOCKS:
        t += label(f"{SIDE[0]}%", f"{y1}%-{lh + 2}", side_w, lh, text, bold(SMALL), ink, "right")
    t += '+ label {\n\tid = "__timeout__"\n' + label("62%", f"90%-{HUGE + 12}", "34%", HUGE + 8, "%d", bold(HUGE), CYAN, "right")[len("+ label {\n"):]
    return t


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
png(os.path.join(out, "background.png"), W, H, background())
r = ITEM // 2
png(os.path.join(out, "select_w.png"), r, ITEM, render(r, ITEM, [(pill(0, 0, 2 * r, ITEM, left=True, right=False), TEAL)]))
png(os.path.join(out, "select_c.png"), 1, ITEM, [[TEAL]] * ITEM)
png(os.path.join(out, "select_e.png"), r, ITEM, render(r, ITEM, [(pill(-r, 0, r, ITEM), TEAL)]))
with open(os.path.join(out, "theme.txt"), "w") as f:
    f.write(theme())
bolds = sorted({SMALL, MEDIUM, LARGE, HUGE})
fonts = [f"bold-{n}.pf2" for n in bolds] + [f"mono-{MONO}.pf2"]
with open(os.path.join(out, "fonts.txt"), "w") as f:
    f.write("".join(f"bold {n}\n" for n in bolds) + f"mono {MONO}\n")
with open(os.path.join(out, "theme.cfg"), "w") as f:
    f.write("if loadfont " + " ".join("$theme_dir/" + n for n in fonts) + "; then\n")
    f.write(f'  set gfxterm_font="DejaVu Sans Mono Regular {MONO}"\n')
    f.write("  set gfxmode=auto\n")
    f.write("  if terminal_output gfxterm; then\n")
    f.write("    set theme=$theme_dir/theme.txt\n")
    f.write("  fi\n")
    f.write("fi\n")
