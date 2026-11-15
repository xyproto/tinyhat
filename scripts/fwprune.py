#!/usr/bin/env python3
import glob
import os
import re
import shutil
import subprocess
import sys

root = sys.argv[1]
# Firmware (glob patterns under usr/lib/firmware) and the modules that load it. Firmware that no module in the
# image loads is removed, such as the firmware for 64-bit era hardware that the i686 kernel has no drivers for
OWNERS = {
    "amdgpu": ["amdgpu"],
    "xe": ["xe"],
    "cirrus": ["snd-hda-scodec-cs35l41*", "snd-hda-scodec-cs35l56*"],
    "intel/sof*": ["snd-sof"],
    "ath10k": ["ath10k_core"],
    "ath11k": ["ath11k"],
    "ath12k": ["ath12k"],
    "rtw89": ["rtw89_core"],
    "mediatek/mt7996": ["mt7996e"],
    "mediatek/WIFI_MT7925*": ["mt7925-common"],
    "mediatek/WIFI_MT7922*": ["mt7921-common"],
    "mediatek/WIFI_MT7961*": ["mt7921-common"],
}
for d, names in OWNERS.items():
    found = [m for n in names for m in glob.glob(os.path.join(root, "usr/lib/modules/*/kernel/**", n + ".ko*"), recursive=True)]
    if found:
        continue
    for path in glob.glob(os.path.join(root, "usr/lib/firmware", d)):
        if os.path.isdir(path) and not os.path.islink(path):
            shutil.rmtree(path)
        else:
            os.remove(path)
        print(f"fwprune.py: removed {os.path.relpath(path, root)}, since no module in this image uses it")
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
# The modules of the older chips (iwldvm, iwlmvm, and iwl3945 and iwl4965 in iwlegacy) declare firmware too
mods += glob.glob(os.path.join(root, "usr/lib/modules/*/kernel/drivers/net/wireless/intel/iwl*/**/*.ko*"), recursive=True)
mods = sorted(set(mods))
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
    if cap is None:
        # No driver in this kernel handles this chip (iwlmvm is left out of the i686 kernel)
        for v in versions:
            path = os.path.join(fwdir, v[1])
            freed += os.path.getsize(path)
            os.remove(path)
        continue
    if not usable:
        continue
    keep = max(usable)
    for v in versions:
        if v != keep:
            path = os.path.join(fwdir, v[1])
            freed += os.path.getsize(path)
            os.remove(path)
for f in os.listdir(fwdir):
    stem = re.sub(r"\.pnvm(\.[a-z]+)?$", "", f)
    if stem != f and not any(name.startswith(stem) for name, _ in wanted):
        path = os.path.join(fwdir, f)
        freed += 0 if os.path.islink(path) else os.path.getsize(path)
        os.remove(path)
top = os.path.join(root, "usr/lib/firmware")
for d, f in [(fwdir, f) for f in os.listdir(fwdir)] + [(top, f) for f in os.listdir(top) if f.startswith("iwlwifi-")]:
    path = os.path.join(d, f)
    if os.path.islink(path) and not os.path.exists(path):
        os.remove(path)
print(f"fwprune.py: removed {freed >> 20} MiB of older iwlwifi firmware")
