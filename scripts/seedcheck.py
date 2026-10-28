#!/usr/bin/env python3
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkg

SCAN = r'''
check() {
	for f; do
		head -c4 "$f" 2>/dev/null | grep -q ELF || continue
		ldd -r "$f" 2>&1 | grep -E "not found|undefined symbol" | grep -v -E "undefined symbol: _?Py" | sed "s|^|$f	|"
	done
}
if [ $# -gt 0 ]; then
	check "$@"
	exit
fi
find /usr/lib /usr/bin -maxdepth 1 -type f ! -name "libthread_db.so*" ! -name "libfl.so*" \( -name "*.so*" -o -path "/usr/bin/*" \) |
	xargs -P "$(nproc)" -n 50 sh -c "$0" "$0"
'''


def broken_files(root):
    r = subprocess.run(["bwrap", "--ro-bind", root, "/", "--dev", "/dev", "--proc", "/proc", "--unshare-all",
                        "--clearenv", "--setenv", "PATH", "/usr/bin", "/bin/sh", "-c", SCAN, SCAN],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"seedcheck.py: scanning {root} failed: {r.stderr.strip()}")
    return {line.split("\t", 1)[0].lstrip("/") for line in r.stdout.splitlines() if "\t" in line}


def files_of(path):
    out = subprocess.run(["bsdtar", "-tf", path], capture_output=True, text=True).stdout
    return [f for f in out.splitlines() if re.match(r"usr/(lib/[^/]+\.so|bin/)", f)]


def main():
    root, lockfile, pkgdir, outfile = sys.argv[1:5]
    broken = broken_files(root)
    lock = pkg.read_lock(lockfile)
    with ThreadPoolExecutor(os.cpu_count()) as ex:
        lists = ex.map(lambda p: files_of(os.path.join(pkgdir, p["filename"])), lock)
    names = sorted({p["name"] for p, files in zip(lock, lists) if broken.intersection(files)})
    with open(outfile, "w") as f:
        f.write("".join(n + "\n" for n in names))
    print(f"seedcheck.py: {len(broken)} broken files in {len(names)} packages", file=sys.stderr)


if __name__ == "__main__":
    main()
