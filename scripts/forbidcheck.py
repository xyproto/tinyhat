import glob
import os
import sys

import distro


def installed(root):
    names = {}
    local = os.path.join(root, "var/lib/pacman/local")
    for entry in sorted(os.listdir(local)) if os.path.isdir(local) else []:
        desc = os.path.join(local, entry, "desc")
        if not os.path.isfile(desc):
            continue
        with open(desc) as f:
            lines = f.read().split("\n")
        for i, line in enumerate(lines):
            if line in ("%NAME%", "%PROVIDES%", "%REPLACES%") and i + 1 < len(lines):
                for v in lines[i + 1:]:
                    if not v:
                        break
                    names.setdefault(v.split("=")[0].split("<")[0].split(">")[0], entry)
    return names


def main():
    root = sys.argv[1]
    image = distro.section("image")
    bad = []
    names = installed(root)
    if not names:
        sys.exit(f"forbidcheck.py: no pacman database in {root}")
    for name in image.get("forbidden", []):
        if name in names:
            bad.append(f"package {name} ({names[name]})")
    for pattern in image.get("forbidden_files", []):
        for path in sorted(glob.glob(os.path.join(root, pattern))):
            bad.append(f"file /{os.path.relpath(path, root)}")
    if bad:
        for b in bad:
            print(f"forbidcheck.py: {root} contains forbidden {b}", file=sys.stderr)
        sys.exit(1)


main()
