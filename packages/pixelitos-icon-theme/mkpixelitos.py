#!/usr/bin/env python3
import os, re, struct, sys, tarfile, zlib

SIZES = (16, 32, 48)


def unfilter(raw, width, height, bpp, stride):
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(height):
        ft, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ft == 1:
                line[x] = (line[x] + a) & 255
            elif ft == 2:
                line[x] = (line[x] + b) & 255
            elif ft == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return rows


def decode(data):
    pos, idat, plte, trns = 8, b"", b"", b""
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    if interlace or ctype not in (2, 3, 6) or (ctype != 3 and depth != 8):
        raise ValueError("unsupported PNG")
    channels = {2: 3, 3: 1, 6: 4}[ctype]
    bpp = max(1, depth * channels // 8)
    rows = unfilter(zlib.decompress(idat), width, height, bpp, (width * depth * channels + 7) // 8)
    pixels = []
    for line in rows:
        out = []
        for x in range(width):
            if ctype == 3:
                bit = x * depth
                v = (line[bit // 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1)
                out.append(tuple(plte[3 * v:3 * v + 3]) + (trns[v] if v < len(trns) else 255,))
            elif ctype == 2:
                out.append(tuple(line[3 * x:3 * x + 3]) + (255,))
            else:
                out.append(tuple(line[4 * x:4 * x + 4]))
        pixels.append(out)
    return width, height, pixels


def encode(width, height, pixels):
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in pixels)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def scale(data, f):
    w, h, px = decode(data)
    return encode(w * f, h * f, [[p for p in row for _ in range(f)] for row in px for _ in range(f)])


def main():
    src, dest = sys.argv[1], sys.argv[2]
    contexts = {}
    with tarfile.open(src) as t:
        top = t.getnames()[0].split("/")[0] + "/pixelitos-dark/"
        index = t.extractfile(top + "index.theme").read().decode()
        for sect, body in re.findall(r"^\[16/([^\]]+)\]\n((?:[^\[\n].*\n?)*)", index, re.M):
            m = re.search(r"^Context=(.+)$", body, re.M)
            contexts[sect] = m.group(1) if m else "Applications"
        for m in t.getmembers():
            if not m.name.startswith(top + "16/"):
                continue
            rel = m.name[len(top) + 3:]
            if not rel:
                continue
            for size in SIZES:
                path = os.path.join(dest, str(size), rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                if m.issym():
                    if not os.path.lexists(path):
                        os.symlink(m.linkname, path)
                elif m.isdir():
                    os.makedirs(path, exist_ok=True)
                elif m.isfile() and m.name.endswith(".png"):
                    data = t.extractfile(m).read()
                    with open(path, "wb") as f:
                        f.write(data if size == 16 else scale(data, size // 16))
        lic = t.extractfile(t.getnames()[0].split("/")[0] + "/LICENSE").read()
    with open(os.path.join(dest, "LICENSE"), "wb") as f:
        f.write(lic)
    dirs = [f"{s}/{c}" for s in SIZES for c in sorted(contexts)]
    with open(os.path.join(dest, "index.theme"), "w") as f:
        f.write("[Icon Theme]\nName=Pixelitos Dark\nComment=16-bit pixel art icons\nInherits=hicolor\nExample=folder\n")
        f.write("Directories=" + ",".join(dirs) + "\n")
        for d in dirs:
            s, c = d.split("/", 1)
            f.write(f"\n[{d}]\nContext={contexts[c]}\nSize={s}\nType=Fixed\n")


main()
