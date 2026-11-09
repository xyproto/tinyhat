#!/usr/bin/env python3
import os
import shutil
import struct
import subprocess
import sys

root = sys.argv[1]
UPX_MIN = 4 << 20
HAVE = {tool: shutil.which(tool) is not None for tool in ("upx", "sstrip")}
for tool, ok in HAVE.items():
    if not ok:
        print(f"elfshrink.py: {tool} not found, skipping it", file=sys.stderr)
UPX_SKIP = {"fish", "fish_indent", "fish_key_reader", "foot", "dwl", "o", "mc"}


def executable(path):
    with open(path, "rb") as f:
        head = f.read(64)
        if len(head) < 52 or head[:4] != b"\x7fELF":
            return False
        bits64, little = head[4] == 2, head[5] == 1
        e = "<" if little else ">"
        etype = struct.unpack_from(e + "H", head, 16)[0]
        if bits64:
            phoff, = struct.unpack_from(e + "Q", head, 32)
            phentsize, phnum = struct.unpack_from(e + "HH", head, 54)
        else:
            phoff, = struct.unpack_from(e + "I", head, 28)
            phentsize, phnum = struct.unpack_from(e + "HH", head, 42)
        f.seek(phoff)
        table = f.read(phentsize * phnum)
    types = [struct.unpack_from(e + "I", table, i * phentsize)[0] for i in range(phnum)]
    return etype == 2 or (etype == 3 and 3 in types)


def packed(path):
    with open(path, "rb") as f:
        return b"UPX!" in f.read(4096)


saved = {"upx": 0, "sstrip": 0}
seen = set()
for top in ("usr/bin", "usr/lib"):
    for dirpath, dirnames, filenames in os.walk(os.path.join(root, top)):
        dirnames[:] = [d for d in dirnames if d not in ("modules", "firmware")]
        for name in filenames:
            path = os.path.join(dirpath, name)
            st = os.lstat(path)
            if not os.path.isfile(path) or os.path.islink(path) or st.st_ino in seen:
                continue
            seen.add(st.st_ino)
            if st.st_mode & 0o6000:
                continue
            try:
                if not executable(path) or packed(path):
                    continue
            except (OSError, struct.error):
                continue
            before = st.st_size
            if (HAVE["upx"] and top == "usr/bin" and before >= UPX_MIN and st.st_nlink == 1 and name not in UPX_SKIP
                    and subprocess.run(["upx", "-q", "-q", "--lzma", path], capture_output=True).returncode == 0):
                saved["upx"] += before - os.path.getsize(path)
                os.chmod(path, st.st_mode & 0o7777)
                continue
            if ".so" in name:
                continue
            if HAVE["sstrip"] and subprocess.run(["sstrip", path], capture_output=True).returncode == 0:
                saved["sstrip"] += before - os.path.getsize(path)
                os.chmod(path, st.st_mode & 0o7777)
print(f"elfshrink.py: upx saved {saved['upx'] >> 20} MiB, sstrip saved {saved['sstrip'] >> 10} KiB (uncompressed)")
