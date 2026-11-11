#!/usr/bin/env python3
import glob
import os
import re
import shutil
import subprocess
import sys

root = sys.argv[1]
OWNERS = {"amdgpu": ["amdgpu"], "xe": ["xe"], "cirrus": ["snd-hda-scodec-cs35l41*", "snd-hda-scodec-cs35l56*"]}
for d, names in OWNERS.items():
    path = os.path.join(root, "usr/lib/firmware", d)
    found = [m for n in names for m in glob.glob(os.path.join(root, "usr/lib/modules/*/kernel/**", n + ".ko*"), recursive=True)]
    if os.path.isdir(path) and not found:
        shutil.rmtree(path)
        print(f"fwprune.py: removed usr/lib/firmware/{d}, since no module in this image uses it")
i915dir = os.path.join(root, "usr/lib/firmware/i915")
i915mods = glob.glob(os.path.join(root, "usr/lib/modules/*/kernel/drivers/gpu/drm/i915/i915.ko*"))
if os.path.isdir(i915dir) and i915mods:
    declared = set()
    for mod in i915mods:
        out = subprocess.run(["modinfo", "-F", "firmware", mod], check=True, capture_output=True, text=True).stdout
        declared |= {n.split("/", 1)[1] for n in out.split() if n.startswith("i915/")}
    keep = set()
    for f in os.listdir(i915dir):
        base = f[:-4] if f.endswith(".zst") else f
        if base in declared:
            keep.add(f)
            path = os.path.join(i915dir, f)
            if os.path.islink(path):
                keep.add(os.path.basename(os.path.realpath(path)))
    freed = 0
    for f in os.listdir(i915dir):
        path = os.path.join(i915dir, f)
        if f not in keep:
            freed += 0 if os.path.islink(path) else os.path.getsize(path)
            os.remove(path)
    print(f"fwprune.py: removed {freed >> 20} MiB of i915 firmware that the i915 module does not ask for")
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
family = {}
for (name, c), v in wanted.items():
    key = ("-".join(name.split("-")[:2]), c)
    family[key] = max(family.get(key, 0), v)
freed = 0
for chip, versions in files.items():
    cap = wanted.get(chip, family.get(("-".join(chip[0].split("-")[:2]), chip[1])))
    usable = [v for v in versions if cap is not None and v[0] <= cap]
    if not usable:
        continue
    keep = max(usable)
    for v in versions:
        if v != keep:
            path = os.path.join(fwdir, v[1])
            freed += os.path.getsize(path)
            os.remove(path)
top = os.path.join(root, "usr/lib/firmware")
for d, f in [(fwdir, f) for f in os.listdir(fwdir)] + [(top, f) for f in os.listdir(top) if f.startswith("iwlwifi-")]:
    path = os.path.join(d, f)
    if os.path.islink(path) and not os.path.exists(path):
        os.remove(path)
print(f"fwprune.py: removed {freed >> 20} MiB of older iwlwifi firmware")
