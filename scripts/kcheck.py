#!/usr/bin/env python3
import sys


def parse(path, fragment):
    out = {}
    for line in open(path):
        line = line.strip()
        if line.startswith("CONFIG_"):
            k, v = line.split("=", 1)
            out[k] = v
        elif not fragment and line.startswith("# CONFIG_") and line.endswith(" is not set"):
            out[line.split()[1]] = "n"
    return out


want = parse(sys.argv[1], True)
have = parse(sys.argv[2], False)
bad = [f"{k}: wanted {v}, got {have.get(k, 'n')}" for k, v in want.items() if have.get(k, "n") != v]
for b in bad:
    print("kconfig:", b, file=sys.stderr)
