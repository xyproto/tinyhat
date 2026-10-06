#!/usr/bin/env python3
import os
import math
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np

os.umask(0o022)

QUALITY = os.environ.get("SF3_QUALITY", "5")
TARGET_PEAK = 29490
GEN_ATTEN, GEN_SAMPLEID = 48, 53
EMU_ATTENUATION_FACTOR = 0.4


def chunks(data, off, end):
    while off < end:
        cid, size = struct.unpack_from("<4sI", data, off)
        yield cid, off + 8, size
        off += 8 + size + (size & 1)


def find_list(data, kind):
    for cid, off, size in chunks(data, 12, len(data)):
        if cid == b"LIST" and data[off:off + 4] == kind:
            return off + 4, off + size
    sys.exit(f"missing LIST {kind}")


def encode(pcm, rate):
    p = subprocess.run(
        ["oggenc", "-Q", "-r", "-B", "16", "-C", "1", "-R", str(rate), "-q", QUALITY, "-o", "-", "-"],
        input=pcm, stdout=subprocess.PIPE, check=True,
    )
    return p.stdout


def signed(v):
    return v - 65536 if v > 32767 else v


def compensate(pdta, comp):
    d = dict(pdta)
    inst = [struct.unpack_from("<20sH", d[b"inst"], i * 22) for i in range(len(d[b"inst"]) // 22)]
    ibag = [list(struct.unpack_from("<HH", d[b"ibag"], i * 4)) for i in range(len(d[b"ibag"]) // 4)]
    igen = [struct.unpack_from("<HH", d[b"igen"], i * 4) for i in range(len(d[b"igen"]) // 4)]
    new_gen, new_bag = [], []
    for ii in range(len(inst) - 1):
        global_att = 0
        for b in range(inst[ii][1], inst[ii + 1][1]):
            gens = list(igen[ibag[b][0]:ibag[b + 1][0]])
            new_bag.append([len(new_gen), ibag[b][1]])
            if not gens or gens[-1][0] != GEN_SAMPLEID:
                if b == inst[ii][1]:
                    global_att = next((signed(a) for o, a in gens if o == GEN_ATTEN), 0)
                new_gen += gens
                continue
            c = comp[gens[-1][1]]
            if c:
                att = next((signed(a) for o, a in gens if o == GEN_ATTEN), None)
                att = global_att if att is None else att
                val = max(0, min(1440, att + round(c / EMU_ATTENUATION_FACTOR)))
                gens = [g for g in gens if g[0] != GEN_ATTEN]
                gens.insert(len(gens) - 1, (GEN_ATTEN, val))
            new_gen += gens
    new_bag.append([len(new_gen), ibag[-1][1]])
    new_gen.append(igen[-1])
    if len(new_gen) > 65535:
        sys.exit("too many instrument generators")
    d[b"ibag"] = bytearray(b"".join(struct.pack("<HH", *b) for b in new_bag))
    d[b"igen"] = bytearray(b"".join(struct.pack("<HH", *g) for g in new_gen))
    return [(c, d[c]) for c, _ in pdta]


def main(src, dst, sf_name=None, sf_note=None):
    with open(src, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"sfbk":
        sys.exit("not a sf2 file")

    info_start, info_end = find_list(data, b"INFO")
    sdta_start, sdta_end = find_list(data, b"sdta")
    pdta_start, pdta_end = find_list(data, b"pdta")

    smpl = next(data[o:o + s] for c, o, s in chunks(data, sdta_start, sdta_end) if c == b"smpl")
    pdta = [(c, bytearray(data[o:o + s])) for c, o, s in chunks(data, pdta_start, pdta_end)]
    shdr = next(b for c, b in pdta if c == b"shdr")

    n = len(shdr) // 46
    hdrs = []
    for i in range(n):
        name, start, end, ls, le, rate, key, corr, link, typ = struct.unpack_from("<20sIIIIIBbHH", shdr, i * 46)
        hdrs.append([name, start, end, ls, le, rate, key, corr, link, typ])

    def job(i):
        h = hdrs[i]
        if i == n - 1 or h[9] & 0x8000 or h[2] <= h[1]:
            return b"", 0
        pcm = np.frombuffer(smpl, "<i2", h[2] - h[1], h[1] * 2).astype(np.float64)
        peak = np.abs(pcm).max()
        if peak == 0:
            return encode(pcm.astype("<i2").tobytes(), h[5]), 0
        cb = round(200 * math.log10(TARGET_PEAK / peak))
        scaled = np.clip(np.rint(pcm * 10 ** (cb / 200)), -32768, 32767).astype("<i2")
        return encode(scaled.tobytes(), h[5]), cb

    with ThreadPoolExecutor(os.cpu_count()) as ex:
        res = list(ex.map(job, range(n)))
    oggs = [r[0] for r in res]
    comp = [r[1] for r in res]
    pdta = compensate(pdta, comp)
    shdr = next(b for c, b in pdta if c == b"shdr")

    out_smpl = bytearray()
    for i, h in enumerate(hdrs):
        ogg = oggs[i]
        if not ogg:
            continue
        start, end = h[1], h[2]
        h[3] -= start
        h[4] -= start
        h[1] = len(out_smpl)
        out_smpl += ogg
        h[2] = len(out_smpl)
        h[9] |= 0x10
        if len(out_smpl) & 1:
            out_smpl += b"\0"
    for i, h in enumerate(hdrs):
        struct.pack_into("<20sIIIIIBbHH", shdr, i * 46, *h)

    info = bytearray()
    for cid, off, size in chunks(data, info_start, info_end):
        body = data[off:off + size]
        if cid == b"ifil":
            body = struct.pack("<HH", 3, 1)
        elif cid == b"INAM" and sf_name:
            body = sf_name.encode() + b"\0"
        elif cid == b"ICMT" and sf_note:
            body = body.rstrip(b"\0") + b"\n\n" + sf_note.encode() + b"\0"
        if len(body) & 1:
            body += b"\0"
        info += struct.pack("<4sI", cid, len(body)) + body

    def lst(kind, body):
        return struct.pack("<4sI4s", b"LIST", len(body) + 4, kind) + body

    sdta = struct.pack("<4sI", b"smpl", len(out_smpl)) + out_smpl
    pd = b"".join(struct.pack("<4sI", c, len(b)) + b for c, b in pdta)
    body = b"sfbk" + lst(b"INFO", bytes(info)) + lst(b"sdta", bytes(sdta)) + lst(b"pdta", pd)
    tmp = dst + ".part"
    with open(tmp, "wb") as f:
        f.write(struct.pack("<4sI", b"RIFF", len(body)) + body)
    os.replace(tmp, dst)


if __name__ == "__main__":
    main(*sys.argv[1:5])
