#!/usr/bin/env python3
import glob
import os
import re
import subprocess
import sys

root = sys.argv[1]
fwdir = os.path.join(root, "usr/lib/firmware/intel/iwlwifi")
mods = glob.glob(os.path.join(root, "usr/lib/modules/*/kernel/drivers/net/wireless/intel/iwlwifi/iwlwifi.ko*"))
if not os.path.isdir(fwdir) or not mods:
    sys.exit(0)
pat = re.compile(r"(.*)-(c?)(\d+)\.ucode(\.[a-z]+)?$")
wanted = {}
for mod in mods:
    out = subprocess.run(["modinfo", "-F", "firmware", mod], check=True, capture_output=True, text=True).stdout
    for name in out.split():
        m = pat.match(name)
        if m:
            k = m.group(1, 2)
            wanted[k] = max(wanted.get(k, 0), int(m.group(3)))
files = {}
for f in os.listdir(fwdir):
    m = pat.match(f)
    if m and not os.path.islink(os.path.join(fwdir, f)):
        files.setdefault(m.group(1, 2), []).append((int(m.group(3)), f))
freed = 0
for chip, versions in files.items():
    usable = [v for v in versions if v[0] <= wanted.get(chip, -1)]
    if not usable:
        continue
    keep = max(usable)
    for v in versions:
        if v != keep:
            path = os.path.join(fwdir, v[1])
            freed += os.path.getsize(path)
            os.remove(path)
for f in os.listdir(fwdir):
    path = os.path.join(fwdir, f)
    if os.path.islink(path) and not os.path.exists(path):
        os.remove(path)
print(f"fwprune.py: removed {freed >> 20} MiB of older iwlwifi firmware")
