#!/usr/bin/env python3
import glob
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import distro  # noqa: E402

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(TOP, "build")
ERROR = re.compile(r"(error:|error\[|\berror\b.*\bfailed\b|ERROR: (?!A failure)|CMake Error|fatal:|undefined reference|"
                   r"not found|No such file|cannot (find|open|stat)|did not pass|Dependency .* found: NO|"
                   r"No rule to make|Killed|out of memory|Error [0-9]+$)")
NOISE = re.compile(r"(checking |Werror|-error|error\.[cho]\b|errors\.|strerror|ferror|libgpg-error|"
                   r"warning:|^\s*\d+ \|)")


def graph(target):
    """The builds that TARGET (an image, or cross) needs, the ones of them taken prebuilt, and the ones that ninja would
    run (that may build, if their cache key changed), from build/plan.json."""
    plan = distro.plan()
    if plan is None:
        sys.exit("buildstatus.py: build/plan.json is missing, run make first")
    builds = plan["builds"]
    group = "cross" if target == "cross" else "rootfs-x32" if "64x32" in target else "rootfs-i686"
    roots = list(plan["groups"].get(group, []))
    if target != "cross":
        with open(os.path.join(TOP, "Makefile")) as f:
            boot = re.search(r"BOOT_PKGS := (.*)", f.read())
        if boot:
            roots += ["i686/" + n for n in re.findall(r"\$\(B\)/pkg/i686/([^ \n]+)", boot.group(1))]
    needed, stack = set(), [r for r in roots if r in builds]
    while stack:
        key = stack.pop()
        if key not in needed:
            needed.add(key)
            stack += builds[key]["deps"]
    prebuilt = {k for k in needed if builds[k]["mode"] == "prebuilt"}
    by_name = {b["name"]: k for k, b in builds.items()}
    # ninja's targets are relative to the top directory
    dry = subprocess.run(["ninja", "-f", "build/pkgs.ninja", "-n", f"build/pkg/group/{group}"],
                         cwd=TOP, capture_output=True, text=True).stdout
    stale = {by_name[m] for m in re.findall(r"^\[\d+/\d+\] (\S+)", dry, re.M) if m in by_name}
    return needed, prebuilt, stale & needed


def running():
    """(seconds, build name) of the package builds that run now; the name is that of build/log/NAME.log."""
    out = subprocess.run(["ps", "-eo", "etimes=,args="], capture_output=True, text=True).stdout
    jobs = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 5 and parts[1] == "/bin/sh" and parts[2] == "scripts/buildpackage.sh" and parts[3] == "--name":
            jobs.append((int(parts[0]), parts[4]))
    return sorted(jobs, reverse=True)


def states():
    """build/state/NAME of every package build (scripts/buildpackage.sh --name): NAME -> {state, start, end, log, reason}."""
    d = os.path.join(B, "state")
    out = {}
    for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if not name.endswith(".tmp"):
            with open(os.path.join(d, name), errors="replace") as f:
                out[name] = dict(line.rstrip("\n").partition("=")[::2] for line in f if "=" in line)
    return out


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
    target = sys.argv[1] if len(sys.argv) > 1 else "build/tinyhat32.img"
    needed, prebuilt, todo = graph(target)
    done = needed - todo
    print(f"{target}: {len(done)}/{len(needed)} package builds up to date "
          f"({len(needed & prebuilt)} prebuilt, {len(needed - prebuilt)} built here), {len(todo)} left")
    img = os.path.join(TOP, target)
    if os.path.exists(img):
        print(f"  image: {os.path.getsize(img) // 1048576} MiB, "
              f"{time.strftime('%F %H:%M', time.localtime(os.path.getmtime(img)))}")
    jobs = running()
    if jobs:
        print("running:")
        for secs, name in jobs:
            print(f"  {minutes(secs):>8}  {name}  {progress(os.path.join(B, 'log', name + '.log'))}")
    # A failure stays listed until the same build succeeds (the build of needed key KIND/BASE is named KIND-BASE)
    names = {k.replace("/", "-", 1) for k in needed}
    failed = {n: st for n, st in states().items() if st.get("state") == "failed" and n in names}
    if failed:
        print(f"failed ({len(failed)}):")
        for name, st in failed.items():
            when = time.strftime("%F %H:%M", time.localtime(float(st.get("end", 0))))
            print(f"  {name} ({when}): {first_error(os.path.join(TOP, st.get('log', '')))}")
    # The newest make run log (scripts/failfast.py)
    runs = glob.glob(os.path.join(B, "log", "make-*.log"))
    if runs:
        with open(max(runs, key=os.path.getmtime), errors="replace") as f:
            text = f.read()
        missing = sorted(set(re.findall(r"^missing .*$", text, re.M)))
        if missing:
            print("missing libraries in the root filesystem:")
            for line in missing[:20]:
                print(f"  {line}")
        if todo and not jobs and not failed:
            print("for ninja to run (each builds if its cache key changed): " + " ".join(sorted(k.split("/", 1)[1] for k in todo))[:400])
    free = shutil.disk_usage(TOP).free // 1073741824
    print(f"disk: {free} GiB free" + ("  (low!)" if free < 10 else ""))


if __name__ == "__main__":
    main()
