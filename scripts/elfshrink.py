#!/usr/bin/env python3
# Removes what executables do not need to run (section headers, sstrip) in usr/bin and usr/lib of a root file system.
# No upx: the image is a compressed squashfs, which compresses unpacked executables better than upx does, and packed
# executables cannot share their pages in RAM
import os
import struct
import subprocess
import sys

root = sys.argv[1]
tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "x86_64-tool.sh")
if subprocess.run([tool, "sstrip", "--version"], capture_output=True).returncode != 0:
    sys.exit("elfshrink.py: sstrip not found in the x86_64 build environment")


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


saved = 0
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
                if not executable(path):
                    continue
            except (OSError, struct.error):
                continue
            before = st.st_size
            if ".so" in name:
                continue
            if subprocess.run([tool, "sstrip", path], capture_output=True).returncode == 0:
                saved += before - os.path.getsize(path)
                os.chmod(path, st.st_mode & 0o7777)
print(f"elfshrink.py: sstrip saved {saved >> 10} KiB (uncompressed)")
