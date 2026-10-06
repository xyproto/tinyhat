#!/usr/bin/env python3
import hashlib
import os
import re
import subprocess
import sys
import tarfile
import urllib.request

os.umask(0o022)

MIRROR = os.environ.get("MIRROR", "https://mirror.archlinux32.org/i686")
REPOS = ["core", "extra"]
TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DBDIR = os.path.join(TOP, "vendor", "db")


def die(msg):
    sys.exit(f"pkg.py: {msg}")


def read_list(path):
    out = []
    if not os.path.exists(path):
        return out
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                out.extend(line.split())
    return out


def download(url, dest):
    tmp = dest + ".part"
    print(f"  get {url}", file=sys.stderr)
    req = urllib.request.Request(url, headers={"User-Agent": "tinyhat-build/1.0"})
    with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
        while chunk := r.read(1 << 20):
            f.write(chunk)
    os.replace(tmp, dest)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def strip_ver(dep):
    return re.split(r"[<>=]", dep, maxsplit=1)[0]


def load_db():
    pkgs, provides = {}, {}
    for repo in REPOS:
        path = os.path.join(DBDIR, f"{repo}.db")
        if not os.path.exists(path):
            die(f"missing {path}, run 'make lock'")
        with tarfile.open(path) as t:
            for m in t:
                if not m.name.endswith("/desc"):
                    continue
                fields, key = {}, None
                for line in t.extractfile(m).read().decode().splitlines():
                    if line.startswith("%") and line.endswith("%"):
                        key = line[1:-1]
                        fields[key] = []
                    elif line and key:
                        fields[key].append(line)
                name = fields["NAME"][0]
                if name in pkgs:
                    continue
                p = {
                    "name": name,
                    "repo": repo,
                    "version": fields["VERSION"][0],
                    "filename": fields["FILENAME"][0],
                    "sha256": fields["SHA256SUM"][0],
                    "depends": [strip_ver(d) for d in fields.get("DEPENDS", [])],
                }
                pkgs[name] = p
                for pv in fields.get("PROVIDES", []):
                    provides.setdefault(strip_ver(pv), []).append(name)
    return pkgs, provides


def parse_prefs(path):
    prefs = {}
    for item in read_list(path):
        k, v = item.split("=", 1)
        prefs[k] = v
    return prefs


def resolve(listfile):
    pkgs, provides = load_db()
    cfg = os.path.dirname(listfile)
    prefs = parse_prefs(os.path.join(cfg, "providers.txt"))
    base = os.path.splitext(os.path.basename(listfile))[0]
    skip = set(read_list(os.path.join(cfg, f"{base}.skip")))
    targets = read_list(listfile)
    chosen, seen, queue = {}, set(), list(targets)
    while queue:
        dep = queue.pop(0)
        if dep in seen or dep in skip:
            continue
        seen.add(dep)
        if dep in prefs:
            name = prefs[dep]
        elif dep in pkgs:
            name = dep
        elif dep in provides:
            cands = sorted(provides[dep])
            name = next((c for c in cands if c in chosen), cands[0])
            if len(cands) > 1 and name not in chosen:
                print(f"  note: {dep} -> {name} (of {' '.join(cands)})", file=sys.stderr)
        else:
            die(f"cannot resolve '{dep}'")
        if name in skip or name in chosen:
            continue
        chosen[name] = pkgs[name]
        queue.extend(pkgs[name]["depends"])
    return [chosen[k] for k in sorted(chosen)]


def cmd_lock(listfile, lockfile):
    os.makedirs(DBDIR, exist_ok=True)
    if os.environ.get("REFRESH") or not all(
        os.path.exists(os.path.join(DBDIR, f"{r}.db")) for r in REPOS
    ):
        for repo in REPOS:
            download(f"{MIRROR}/{repo}/{repo}.db", os.path.join(DBDIR, f"{repo}.db"))
    sel = resolve(listfile)
    with open(lockfile, "w") as f:
        for p in sel:
            f.write(f"{p['repo']} {p['name']} {p['version']} {p['filename']} {p['sha256']}\n")
    print(f"{lockfile}: {len(sel)} packages", file=sys.stderr)


def read_lock(lockfile):
    out = []
    with open(lockfile) as f:
        for line in f:
            if line.strip():
                repo, name, ver, fn, h = line.split()
                out.append({"repo": repo, "name": name, "version": ver, "filename": fn, "sha256": h})
    return out


def cmd_fetch(lockfile, pkgdir):
    os.makedirs(pkgdir, exist_ok=True)
    for p in read_lock(lockfile):
        dest = os.path.join(pkgdir, p["filename"])
        if os.path.exists(dest) and sha256(dest) == p["sha256"]:
            continue
        download(f"{MIRROR}/{p['repo']}/{p['filename']}", dest)
        if sha256(dest) != p["sha256"]:
            os.unlink(dest)
            die(f"checksum mismatch for {p['filename']}")


def read_sources(path):
    out = {}
    with open(path) as f:
        for line in f:
            fields = line.split()
            if fields and not fields[0].startswith("#"):
                name, h, url = fields
                out[name] = (h, url)
    return out


def cmd_source(sourcesfile, srcdir, *names):
    src = read_sources(sourcesfile)
    os.makedirs(srcdir, exist_ok=True)
    for name in names or src:
        if name not in src:
            die(f"{name} is not listed in {sourcesfile}")
        h, url = src[name]
        dest = os.path.join(srcdir, name)
        if os.path.exists(dest) and sha256(dest) == h:
            continue
        download(url, dest)
        if sha256(dest) != h:
            os.unlink(dest)
            die(f"checksum mismatch for {name}")


def cmd_extract(lockfile, pkgdir, root, excludes=()):
    os.makedirs(root, exist_ok=True)
    pats = [".PKGINFO", ".BUILDINFO", ".MTREE", ".INSTALL", ".CHANGELOG"] + list(excludes)
    args = []
    for pat in pats:
        args += ["--exclude", pat]
    if os.geteuid() == 0:
        args += ["-p", "--numeric-owner"]
    for p in read_lock(lockfile):
        subprocess.run(
            ["bsdtar", "--no-xattrs", "--no-acls", "--no-fflags", "-xf", os.path.join(pkgdir, p["filename"]), "-C", root] + args,
            check=True,
        )


def main():
    if len(sys.argv) < 2:
        die("usage: pkg.py lock LIST LOCK | fetch LOCK DIR | extract LOCK DIR ROOT [EXCLUDEFILE] | source SOURCES DIR [NAME...]")
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "lock":
        cmd_lock(*args)
    elif cmd == "fetch":
        cmd_fetch(*args)
    elif cmd == "source":
        cmd_source(*args)
    elif cmd == "extract":
        ex = read_list(args[3]) if len(args) > 3 else []
        cmd_extract(args[0], args[1], args[2], ex)
    else:
        die(f"unknown command {cmd}")


if __name__ == "__main__":
    main()
