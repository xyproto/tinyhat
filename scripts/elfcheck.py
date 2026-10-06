#!/usr/bin/env python3
import os
import struct
import sys

root = sys.argv[1]
LIBDIRS = ["usr/lib", "usr/lib/systemd"]


def dyninfo(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 1:
        return None
    e_phoff, = struct.unpack_from("<I", data, 28)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 42)
    loads, dyn = [], None
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIIII", data, e_phoff + i * e_phentsize)
        if p_type == 1:
            loads.append((p_vaddr, p_offset, p_filesz))
        elif p_type == 2:
            dyn = (p_offset, p_filesz)
    if not dyn:
        return [], []

    def off(vaddr):
        for va, of, sz in loads:
            if va <= vaddr < va + sz:
                return vaddr - va + of
        return None

    needed, runpath, strtab = [], [], None
    for i in range(0, dyn[1], 8):
        tag, val = struct.unpack_from("<iI", data, dyn[0] + i)
        if tag == 0:
            break
        if tag == 1:
            needed.append(val)
        elif tag in (15, 29):
            runpath.append(val)
        elif tag == 5:
            strtab = off(val)
    if strtab is None:
        return [], []

    def s(o):
        return data[strtab + o:data.index(b"\0", strtab + o)].decode()

    return [s(n) for n in needed], [p for r in runpath for p in s(r).split(":")]


have = set()
for d in LIBDIRS:
    for name in os.listdir(os.path.join(root, d)):
        have.add(name)
missing = {}
for dirpath, _, files in os.walk(root):
    for fn in files:
        path = os.path.join(dirpath, fn)
        if os.path.islink(path) or not os.path.isfile(path):
            continue
        try:
            info = dyninfo(path)
        except (OSError, struct.error, ValueError, IndexError):
            continue
        if not info:
            continue
        needed, runpath = info
        rel = os.path.relpath(path, root)
        here = os.path.dirname(rel)
        for lib in needed:
            if lib in have:
                continue
            dirs = [p.replace("$ORIGIN", "/" + here).lstrip("/") for p in runpath]
            if any(os.path.exists(os.path.join(root, d, lib)) for d in dirs):
                continue
            missing.setdefault(lib, []).append(rel)
for lib, users in sorted(missing.items()):
    print(f"missing {lib}: {' '.join(sorted(users)[:6])}{' ...' if len(users) > 6 else ''}")
sys.exit(1 if missing else 0)
