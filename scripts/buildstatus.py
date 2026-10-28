#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import sys
import time

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(TOP, "build")
ERROR = re.compile(r"(error:|error\[|\berror\b.*\bfailed\b|ERROR: (?!A failure)|CMake Error|fatal:|undefined reference|"
                   r"not found|No such file|cannot (find|open|stat)|did not pass|Dependency .* found: NO|"
                   r"No rule to make|Killed|out of memory|Error [0-9]+$)")
NOISE = re.compile(r"(checking |Werror|-error|error\.[cho]\b|errors\.|strerror|ferror|libgpg-error|"
                   r"warning:|^\s*\d+ \|)")


def graph(target):
    rules, prebuilt, roots = {}, set(), []
    path = os.path.join(B, "pkgs.mk")
    if not os.path.exists(path):
        sys.exit("buildstatus.py: build/pkgs.mk is missing, run make first")
    with open(path) as f:
        lines = f.read().splitlines()
    var = "ROOTFS_X32_STAMPS" if "64x32" in target else "ROOTFS_I686_STAMPS"
    for i, line in enumerate(lines):
        m = re.match(r"^\$\(B\)/pkg/(\w+)/([^:]+):(.*)", line)
        if m:
            key = f"{m.group(1)}/{m.group(2)}"
            rules[key] = [d for d in re.findall(r"\$\(B\)/pkg/(\w+/[^ ]+)", m.group(3)) if not d.startswith("mode/")]
            if i + 2 < len(lines) and "pkg.py prebuilt" in lines[i + 2]:
                prebuilt.add(key)
        elif line.startswith(var + " :="):
            roots = re.findall(r"pkg/(\w+/[^ ]+)", line)
    with open(os.path.join(TOP, "Makefile")) as f:
        mk = f.read()
    boot = re.search(r"BOOT_PKGS := (.*)", mk)
    if boot:
        roots += ["i686/" + n for n in re.findall(r"\$\(B\)/pkg/i686/([^ \n]+)", boot.group(1))]
    needed, stack = set(), list(roots)
    while stack:
        key = stack.pop()
        if key not in needed:
            needed.add(key)
            stack += rules.get(key, [])
    return needed, prebuilt


def running():
    out = subprocess.run(["ps", "-eo", "etimes=,args="], capture_output=True, text=True).stdout
    jobs = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[1] == "/bin/sh" and parts[2] == "scripts/makepkg.sh":
            args = [a for a in parts[3:] if not a.startswith("--") and a not in ("x32",)]
            arch = "boot" if "--bootstrap" in parts else "x32" if "--arch" in parts and "x32" in parts else "i686"
            jobs.append((int(parts[0]), arch, os.path.basename(args[0]) if args else "?"))
    return sorted(jobs, reverse=True)


def progress(log):
    try:
        with open(log, "rb") as f:
            f.seek(max(0, os.path.getsize(log) - 65536))
            tail = f.read().decode(errors="replace")
    except OSError:
        return ""
    steps = re.findall(r"\[(\d+)/(\d+)\]", tail)
    if steps:
        done, total = map(int, steps[-1])
        return f"{done}/{total} ({100 * done // max(total, 1)}%)"
    return ""


def first_error(log):
    try:
        with open(log, errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        return "(no log)"
    hits = [l.strip() for l in lines if ERROR.search(l) and not NOISE.search(l)]
    return (hits[0] if hits else (lines[-3].strip() if len(lines) >= 3 else "(empty log)"))[:150]


def minutes(s):
    return f"{s // 3600}h{s % 3600 // 60:02d}m" if s >= 3600 else f"{s // 60}m{s % 60:02d}s"


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "tinyhat32.img"
    needed, prebuilt = graph(target)
    done = {k for k in needed if os.path.exists(os.path.join(B, "pkg", k))}
    todo = needed - done
    print(f"{target}: {len(done)}/{len(needed)} packages done "
          f"({len(needed & prebuilt)} prebuilt, {len(needed - prebuilt)} built here), {len(todo)} left")
    img = os.path.join(TOP, target)
    if os.path.exists(img):
        print(f"  image: {os.path.getsize(img) // 1048576} MiB, "
              f"{time.strftime('%F %H:%M', time.localtime(os.path.getmtime(img)))}")
    jobs = running()
    if jobs:
        print("running:")
        for secs, arch, base in jobs:
            p = progress(os.path.join(B, "log", f"{arch}-{base}.log"))
            print(f"  {minutes(secs):>8}  {arch}/{base}  {p}")
    full = os.path.join(B, "full.log")
    if os.path.exists(full):
        with open(full, errors="replace") as f:
            text = f.read()
        failed = sorted(set(re.findall(r"build/pkgs.mk:\d+: build/pkg/(\w+/[^\]]+)\] Error", text)))
        if failed:
            print(f"failed in the last pass ({len(failed)}):")
            for key in failed:
                arch, base = key.split("/", 1)
                print(f"  {key}: {first_error(os.path.join(B, 'log', f'{arch}-{base}.log'))}")
        missing = sorted(set(re.findall(r"^missing .*$", text, re.M)))
        if missing:
            print("missing libraries in the root filesystem:")
            for line in missing[:20]:
                print(f"  {line}")
        if todo and not jobs and not failed:
            print("not built yet: " + " ".join(sorted(k.split("/", 1)[1] for k in todo))[:400])
    free = shutil.disk_usage(TOP).free // 1073741824
    print(f"disk: {free} GiB free" + ("  (low!)" if free < 10 else ""))


if __name__ == "__main__":
    main()
