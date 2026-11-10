import glob
import os
import sys

import distro
import mkpkgs


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


def versions_of(root):
    out = {}
    local = os.path.join(root, "var/lib/pacman/local")
    for entry in os.listdir(local) if os.path.isdir(local) else []:
        desc = os.path.join(local, entry, "desc")
        if not os.path.isfile(desc):
            continue
        with open(desc) as f:
            lines = f.read().split("\n")
        fields = {lines[i]: lines[i + 1] for i in range(len(lines) - 1) if lines[i] in ("%NAME%", "%VERSION%")}
        if "%NAME%" in fields and "%VERSION%" in fields:
            out[fields["%NAME%"]] = fields["%VERSION%"]
    return out


def main():
    root = sys.argv[1]
    names = installed(root)
    if not names:
        sys.exit(f"forbidcheck.py: no pacman database in {root}")
    versions = versions_of(root)
    rules = mkpkgs.deny_rules()
    bad = []
    for name, entry in sorted(names.items()):
        if mkpkgs.denied(name, versions.get(name), rules):
            bad.append(f"package {name} ({entry})")
    for pattern in distro.deny_files():
        for p in sorted(glob.glob(os.path.join(root, pattern))):
            bad.append(f"file /{os.path.relpath(p, root)}")
    if bad:
        for b in bad:
            print(f"forbidcheck.py: {root} contains {b}, which [deny] keeps out", file=sys.stderr)
        sys.exit(1)


main()
