#!/usr/bin/env python3
import os, struct, sys, zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkgrubtheme


def decode(path):
    data = open(path, "rb").read()
    pos, idat, plte = 8, b"", b""
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = body
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    if interlace or ctype != 3 or depth not in (1, 2, 4, 8):
        raise ValueError("unsupported PNG")
    stride = (width * depth + 7) // 8
    raw = zlib.decompress(idat)
    lines = np.frombuffer(raw, np.uint8).reshape(height, stride + 1)
    if lines[:, 0].any():
        raise ValueError("unsupported PNG row filter")
    bits = np.unpackbits(lines[:, 1:], axis=1).reshape(height, -1, depth)
    index = (bits * (1 << np.arange(depth - 1, -1, -1, dtype=np.uint8))).sum(axis=2)[:, :width]
    return np.frombuffer(plte, np.uint8).reshape(-1, 3)[index]


def weights(src, dst):
    edges = np.arange(dst + 1) * src / dst
    w = np.zeros((dst, src), np.float32)
    for i in range(dst):
        a, b = edges[i], edges[i + 1]
        for j in range(int(a), min(src, int(np.ceil(b)))):
            w[i, j] = min(b, j + 1) - max(a, j)
    return w / w.sum(axis=1, keepdims=True)


src, dst, width, height = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
img = decode(src).astype(np.float32)
wy, wx = weights(img.shape[0], height), weights(img.shape[1], width)
out = np.stack([wy @ img[:, :, c] @ wx.T for c in range(3)], axis=2)
mkgrubtheme.png(dst, width, height, np.clip(out + 0.5, 0, 255).astype(np.uint8).tolist())
