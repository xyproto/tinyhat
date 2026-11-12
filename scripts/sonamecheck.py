#!/usr/bin/env python3
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkg

DBS = {"i686": None, "x32": None, "x86_64": "x86_64", "aarch64": "aarch64"}


def arch_index(arch):
    if DBS[arch] is None:
        return pkg.load_db()[0]
    return pkg.arch_db(DBS[arch])[0]


def main():
    root, lockfile, arch = sys.argv[1:4]
    warn = DBS[arch] is None
    index = arch_index(arch)
    missing = []
    for p in pkg.read_lock(lockfile):
        if p["repo"] != "tinyhat":
            continue
        theirs = index.get(p["name"])
        if not theirs:
            continue
        for pv in theirs.get("provides_full", []):
            m = re.fullmatch(r"(lib[^=]+\.so)=(\d+)-(32|64)", pv)
            if not m:
                continue
            lib = f"{m.group(1)}.{m.group(2)}"
            dirs = [os.path.join(root, d) for d in ("usr/lib", "usr/lib32")]
            if any(os.path.lexists(os.path.join(d, lib)) for d in dirs):
                continue
            ours = sorted({os.path.basename(f) for d in dirs for f in glob.glob(os.path.join(d, m.group(1) + ".[0-9]*"))
                           if re.fullmatch(re.escape(m.group(1)) + r"\.\d+", os.path.basename(f))})
            if ours:
                missing.append(f"{p['name']}: the image has {', '.join(ours)}, but programs built against the "
                               f"prebuilt {p['name']} {theirs['version']} need {lib}")
    if missing:
        msg = ("sonamecheck.py: libraries with another version than in the prebuilt repositories, "
               "so prebuilt programs that use them will not start:\n  " + "\n  ".join(missing))
        if warn:
            print(msg, file=sys.stderr)
        else:
            sys.exit(msg)


if __name__ == "__main__":
    main()
