#!/usr/bin/env python3
import os, struct, sys, zlib

MODES = [(320, 200), (640, 400), (800, 600), (1024, 768), (1680, 1050), (1920, 1080), (1600, 1200)]
BLACK = (0, 0, 0)
ORANGE = (255, 153, 0)
PEACH = (255, 204, 153)
LAVENDER = (204, 153, 204)
VIOLET = (153, 153, 255)
RED = (204, 102, 102)
GOLD = (255, 170, 68)


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


def render(w, h, shapes, bg=BLACK):
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


def build(out, w, h, entries=6):
    sx, sy = w / 1024, h / 768
    sr = min(sx, sy)

    def X(v):
        return round(v * sx)

    def Y(v):
        return round(v * sy)

    small, medium, large, mono = max(8, round(14 * sy)), max(10, round(20 * sy)), max(12, round(32 * sy)), max(8, round(16 * sy))
    side0, side1 = X(24), X(152)
    blocks = [
        (Y(184), Y(260), LAVENDER, "11-386"),
        (Y(268), Y(340), VIOLET, "04-486"),
        (Y(348), Y(448), PEACH, "07-586"),
        (Y(456), Y(516), RED, "09-686"),
    ]
    top0, top1, bot0, bot1 = Y(24), Y(64), Y(704), Y(744)
    shapes = [
        (elbow(side0, side1, top0, top1, Y(176), X(600), 64 * sr, 28 * sr, True), ORANGE),
        (rect(X(608), top0, X(720), top1), VIOLET),
        (pill(X(932), top0, X(1000), top1), PEACH),
        (elbow(side0, side1, bot0, bot1, Y(524), X(600), 64 * sr, 28 * sr, False), GOLD),
        (rect(X(608), bot0, X(640), bot1), LAVENDER),
        (pill(X(908), bot0, X(1000), bot1), ORANGE),
    ]
    shapes += [(rect(side0, y0, side1, y1), color) for y0, y1, color, _ in blocks]

    item = round(medium * 1.8)
    bar = h >= 400
    pbar = max(28, small + max(2, Y(8))) if bar else 0
    ptop = bot0 - Y(18) - pbar
    mleft, mtop, mwidth = X(192), Y(96), X(800)
    mheight = ptop - Y(8) - mtop
    spacing = max(2, min(round(medium * 0.6), (mheight - entries * item) // entries))
    lh = small + max(2, Y(8))

    theme = f"""desktop-image: "background.png"
desktop-image-scale-method: "stretch"
desktop-color: "#000000"
title-text: ""
terminal-font: "DejaVu Sans Mono Regular {mono}"
terminal-left: "{mleft}"
terminal-top: "{mtop}"
terminal-width: "{mwidth}"
terminal-height: "{ptop + pbar - mtop}"
terminal-border: "0"

+ boot_menu {{
	left = {mleft}
	top = {mtop}
	width = {mwidth}
	height = {mheight}
	item_font = "{bold(medium)}"
	selected_item_font = "{bold(medium)}"
	item_color = "{hexc(PEACH)}"
	selected_item_color = "#000000"
	item_height = {item}
	item_padding = {max(2, round(medium * 0.2))}
	item_spacing = {spacing}
	icon_width = 0
	icon_height = 0
	item_icon_space = 0
	selected_item_pixmap_style = "select_*.png"
	scrollbar = false
}}
"""
    if bar:
        theme += f"""+ progress_bar {{
	id = "__timeout__"
	left = {mleft}
	top = {ptop}
	width = {X(400)}
	height = {pbar}
	font = "{bold(small)}"
	text = "AUTO BOOT IN %d SECONDS"
	text_color = "#000000"
	fg_color = "{hexc(ORANGE)}"
	bg_color = "{hexc(PEACH)}"
	border_color = "#000000"
}}
"""
    theme += label(X(728), top0 + (top1 - top0 - large - 6) // 2, X(196), large + 6, "TINY HAT", bold(large), ORANGE, "right")
    if not bar:
        theme += '+ label {\n\tid = "__timeout__"\n' + label(X(648), bot0 + (bot1 - bot0 - lh) // 2, X(252), lh, "BOOT IN %d", bold(small), LAVENDER, "center")[len("+ label {\n"):]
    elif w >= 800:
        theme += label(X(648), bot0 + (bot1 - bot0 - lh) // 2, X(252), lh, "ENTER BOOT   E EDIT   C CONSOLE", bold(small), LAVENDER, "center")
    pad = max(2, X(8))
    theme += label(side0, Y(172) - lh, side1 - side0 - pad, lh, "BOOT-1", bold(small), BLACK, "right")
    theme += label(side0, Y(528), side1 - side0 - pad, lh, "32-BIT", bold(small), BLACK, "right")
    for y0, y1, _, text in blocks:
        theme += label(side0, y1 - lh, side1 - side0 - pad, lh, text, bold(small), BLACK, "right")

    os.makedirs(out, exist_ok=True)
    png(os.path.join(out, "background.png"), w, h, render(w, h, shapes))
    r = item // 2
    png(os.path.join(out, "select_w.png"), r, item, render(r, item, [(pill(0, 0, 2 * r, item, left=True, right=False), ORANGE)]))
    png(os.path.join(out, "select_c.png"), 1, item, [[ORANGE]] * item)
    png(os.path.join(out, "select_e.png"), r, item, render(r, item, [(pill(-r, 0, r, item), ORANGE)]))
    with open(os.path.join(out, "theme.txt"), "w") as f:
        f.write(theme)
    return {small, medium, large}, {mono}


out = sys.argv[1]
bolds, monos = set(), set()
for w, h in MODES:
    b, m = build(os.path.join(out, f"{w}x{h}"), w, h)
    bolds |= b
    monos |= m
fonts = [f"bold-{n}.pf2" for n in sorted(bolds)] + [f"mono-{n}.pf2" for n in sorted(monos)]
with open(os.path.join(out, "fonts.txt"), "w") as f:
    f.write("".join(f"bold {n}\n" for n in sorted(bolds)) + "".join(f"mono {n}\n" for n in sorted(monos)))
with open(os.path.join(out, "theme.cfg"), "w") as f:
    f.write("if loadfont " + " ".join("$theme_dir/" + n for n in fonts) + "; then\n")
    f.write(f'  set gfxterm_font="DejaVu Sans Mono Regular {min(monos)}"\n')
    f.write("  for mode in " + " ".join(f"{w}x{h}" for w, h in MODES) + "; do\n")
    f.write('    if [ -z "$theme" ]; then\n')
    f.write("      set gfxmode=$mode\n")
    f.write("      if terminal_output gfxterm; then\n")
    f.write("        set theme=$theme_dir/$mode/theme.txt\n")
    f.write("      fi\n")
    f.write("    fi\n")
    f.write("  done\n")
    f.write("fi\n")
    kernel = ",".join(f"{w}x{h}x{d}" for w, h in MODES for d in (32, 24, 16, 15))
    f.write('if [ "$grub_platform" = efi ]; then\n')
    f.write(f"  set kernel_gfxpayload={kernel},keep\n")
    f.write("else\n")
    f.write(f"  set kernel_gfxpayload={kernel},text\n")
    f.write("fi\n")
