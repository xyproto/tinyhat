#!/usr/bin/env python3
import struct
import sys

img, boot_img, core_img = sys.argv[1:4]
boot = bytearray(open(boot_img, "rb").read())
core = open(core_img, "rb").read()
if len(boot) != 512:
    sys.exit("boot.img must be 512 bytes")
if 512 + len(core) > 2048 * 512:
    sys.exit("core.img does not fit before the first partition")
struct.pack_into("<Q", boot, 0x5C, 1)
boot[0x66:0x68] = b"\x90\x90"
with open(img, "r+b") as f:
    f.seek(0)
    f.write(boot[:440])
    f.seek(512)
    f.write(core)
