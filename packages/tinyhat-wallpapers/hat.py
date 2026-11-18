import math, struct, zlib

LOW, SEGMENTS, HAT_SCALE = 64, 16, 2


def png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def project(angle, tilt, x, y, z):
    x1, z1 = x * math.cos(angle) + z * math.sin(angle), -x * math.sin(angle) + z * math.cos(angle)
    y1 = y - 0.45
    y2, z2 = y1 * math.cos(tilt) - z1 * math.sin(tilt), y1 * math.sin(tilt) + z1 * math.cos(tilt)
    f = 3.4 / (z2 + 3.4)
    return round(LOW / 2 + x1 * f * 25), round(LOW / 2 - y2 * f * 25), z2


def hat(angle, ink, scale=HAT_SCALE):
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
    return [[ink[low[y // scale][x // scale]] for x in range(LOW * scale)] for y in range(LOW * scale)]
