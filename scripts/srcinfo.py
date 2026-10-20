#!/usr/bin/env python3
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importpkg

TOP = importpkg.TOP


def stale(d):
    info = os.path.join(d, ".SRCINFO")
    if not os.path.exists(info):
        return True
    t = os.path.getmtime(info)
    return any(os.path.getmtime(os.path.join(d, f)) > t for f in os.listdir(d) if f != ".SRCINFO")


def refresh(d):
    old = open(os.path.join(d, ".SRCINFO")).read() if os.path.exists(os.path.join(d, ".SRCINFO")) else None
    try:
        new = importpkg.srcinfo(d)
    except subprocess.CalledProcessError as e:
        return d, f"failed: {e.stderr.strip().splitlines()[-1] if e.stderr.strip() else e}"
    return d, "unchanged" if new == old else "updated"


def main():
    dirs = sys.argv[1:] or [os.path.join(TOP, "pkgbuilds", sub, base)
                            for sub in ("arch", "tinyhat")
                            for base in sorted(os.listdir(os.path.join(TOP, "pkgbuilds", sub)))]
    todo = [d.rstrip("/") for d in dirs if os.path.isfile(os.path.join(d, "PKGBUILD")) and (sys.argv[1:] or stale(d))]
    failed = False
    with ThreadPoolExecutor(os.cpu_count()) as ex:
        for d, result in ex.map(refresh, todo):
            if result != "unchanged":
                print(f"srcinfo.py: {os.path.relpath(d, TOP)}: {result}")
            failed |= result.startswith("failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
