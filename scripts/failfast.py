#!/usr/bin/env python3
"""Runs make with the given arguments and stops the whole build at the first error. The output also goes to
build/log/make-GOALS.log (like make-cross.log), and the package builds that failed are printed last."""
import os
import re
import signal
import subprocess
import sys
import time

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = os.path.join(TOP, "build", "state")
started = time.time()


def failed_builds():
    """The package builds that failed during this run, from build/state (written by scripts/buildpackage.sh --name)."""
    out = []
    for name in sorted(os.listdir(STATE)) if os.path.isdir(STATE) else []:
        if name.endswith(".tmp"):
            continue
        with open(os.path.join(STATE, name), errors="replace") as f:
            st = dict(line.rstrip("\n").partition("=")[::2] for line in f if "=" in line)
        if st.get("state") == "failed" and float(st.get("end", 0)) >= started:
            out.append(f"failfast.py: FAILED {name}: {st.get('reason', '')}  (see {st.get('log', '')})")
    return out


goals = [a for a in sys.argv[1:] if not a.startswith("-") and "=" not in a] or ["all"]
os.makedirs(os.path.join(TOP, "build", "log"), exist_ok=True)
log = open(os.path.join(TOP, "build", "log", "make-" + "-".join(goals).replace("/", "_")[:100] + ".log"), "wb")


def out(data):
    for f in (sys.stdout.buffer, log):
        f.write(data)
        f.flush()


# make's error, or ninja's (which would otherwise let the builds that run finish first)
error = re.compile(rb"^(make(\[\d+\])?: \*\*\* .*Error \d+|FAILED: )")
p = subprocess.Popen(["make", *sys.argv[1:]], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
failed = False
for line in p.stdout:
    out(line)
    if not failed and error.match(line):
        failed = True
        out(b"failfast.py: stopping the build at the first error\n")
        os.killpg(p.pid, signal.SIGTERM)
        time.sleep(5)
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        break
rc = p.wait()
# The failed builds come last, so that they cannot be missed
if failed or rc:
    for line in failed_builds():
        out(line.encode() + b"\n")
sys.exit(2 if failed else rc)
