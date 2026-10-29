#!/usr/bin/env python3
"""Update the distro configuration to newer package versions, without building anything.

1. Imported Arch recipes whose upstream version is newer in the Arch Linux package databases (core and extra)
   get their pkgver/pkgrel bumped to Arch's version, with checksums regenerated from the upstream sources.
   Releases that only repackage the same upstream version are reported, not applied.
2. Tiny Hat's own recipes that have an nvchecker entry in distro.toml move to the latest upstream release.
3. Imported recipes where upstream (nvchecker) is ahead of Arch are only reported.
4. The archlinux32 package databases and the buildroot lock are refreshed (like "make lock").

No requests are sent to gitlab.archlinux.org. Updates that would make the image depend on a package in
[image] deny (X11, perl, SDL2, GTK2, Qt, ...) are reverted and reported as held back. Bumped recipes keep
their previous Arch packaging (patches, dependencies); "importpkg.py BASE" brings in Arch's exact recipe.
The report is written to build/autoupdate.md, for use as a pull request description.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import tomllib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import distro
import importpkg
import mkpkgs
import pkg

TOP = distro.TOP
REPORT = os.path.join(TOP, "build", "autoupdate.md")
ARCH_MIRROR = os.environ.get("ARCH_MIRROR", "https://geo.mirror.pkgbuild.com")
ARCH_REPOS = ("core", "extra")
ARCH_DB = os.path.join(TOP, "build", "cache", "archdb")
FORBIDDEN_HOST = "gitlab.archlinux.org"
# Be gentle with github.com and the other upstream hosts
NVCHECKER_CONCURRENCY = 4


def log(msg):
    print(f"autoupdate: {msg}", file=sys.stderr, flush=True)


def vercmp(a, b):
    return int(subprocess.run(["vercmp", a, b], capture_output=True, text=True, check=True).stdout)


def image_state():
    """The packages in the image and their dependency edges, resolved from the recipes' .SRCINFO files.
    Edges to skipped packages (like perl) are kept, since the skip list hides them from the package set."""
    infos = mkpkgs.load()
    versions = {n: i["version"] for i in infos for n in i["pkgnames"]}
    targets, prefs, skip = pkg.list_config("image")
    pkgs = pkg.resolve_targets(targets, mkpkgs.indexes(infos), "i686", prefs, skip)
    names = {p["name"] for p in pkgs}
    return names, {(p["name"], d) for p in pkgs for d in p["depends"]}, {n: versions.get(n) for n in names}


def guard(before, after):
    """Return what an update newly pulls into the image that matches [image] deny."""
    rules = mkpkgs.deny_rules()

    def found(state):
        names, edges, versions = state
        out = {f"{n} {versions[n] or ''}".strip() for n in names if mkpkgs.denied(n, versions[n], rules)}
        return out | {f"{a} → {b}" for a, b in edges if mkpkgs.denied(b, None, rules)}

    return sorted(found(after) - found(before))


class ManualUpdate(Exception):
    """The recipe cannot be bumped automatically."""


class Snapshot:
    """The files an update can touch, so that a held back or failed update can be reverted."""

    def __init__(self, base):
        self.dir = os.path.join(TOP, "packages", base)
        self.tmp = tempfile.mkdtemp(prefix="autoupdate-")
        shutil.copytree(self.dir, os.path.join(self.tmp, "pkg"), symlinks=True)
        self.texts = {p: open(p).read() for p in (distro.DISTRO, distro.LOCK)}

    def restore(self):
        shutil.rmtree(self.dir)
        shutil.copytree(os.path.join(self.tmp, "pkg"), self.dir, symlinks=True)
        for p, text in self.texts.items():
            distro.write_if_changed(p, text)
        self.drop()

    def drop(self):
        shutil.rmtree(self.tmp, ignore_errors=True)


STATE = []


def apply(base, change, report, label):
    """Run change() for one package, then keep it, or revert it if it fails or hits the deny list."""
    snap = Snapshot(base)
    before = STATE[0]
    try:
        change()
        after = image_state()
    except ManualUpdate as e:
        snap.restore()
        report["manual"].append((base, label, str(e)))
        return False
    except (Exception, SystemExit) as e:
        snap.restore()
        report["failed"].append((base, label, str(e).strip() or type(e).__name__))
        return False
    hits = guard(before, after)
    if hits:
        snap.restore()
        report["held"].append((base, label, ", ".join(hits)))
        return False
    snap.drop()
    STATE[0] = after
    return True


def arch_versions():
    """The current version of every source package in the Arch Linux package databases, by pkgbase."""
    os.makedirs(ARCH_DB, exist_ok=True)
    out = {}
    for repo in ARCH_REPOS:
        path = os.path.join(ARCH_DB, f"{repo}.db")
        url = f"{ARCH_MIRROR}/{repo}/os/x86_64/{repo}.db"
        subprocess.run(["curl", "-sSfL", "--retry", "3", "--connect-timeout", "30", "--max-time", "300",
                        "-o", path, url], check=True)
        with tarfile.open(path) as t:
            for m in t:
                if not m.name.endswith("/desc"):
                    continue
                fields = pkg.parse_desc(t.extractfile(m).read().decode())
                out[fields.get("BASE", fields["NAME"])[0]] = fields["VERSION"][0]
    return out


def arch_updates(report, only):
    recipes = distro.lock().get("recipes", {})
    tables = distro.tables()
    skipped = distro.skipped()
    bases = [b for b, r in sorted(recipes.items())
             if (not only or b in only) and b not in skipped and tables.get(b, {}).get("source", "arch") == "arch"
             and r.get("version") not in (None, "main", "latest") and "version" not in tables.get(b, {})
             and os.path.isdir(os.path.join(TOP, "packages", b))]
    log(f"checking {len(bases)} imported recipes against the Arch Linux package databases")
    try:
        arch = arch_versions()
    except (subprocess.CalledProcessError, OSError, tarfile.TarError) as e:
        report["failed"].append(("Arch Linux", "package databases", str(e)))
        return
    for base in bases:
        old, new = recipes[base]["version"], arch.get(base)
        if not new or vercmp(new, old) <= 0:
            continue
        o, n = importpkg.split_version(old), importpkg.split_version(new)
        if (o[0], o[1]) == (n[0], n[1]):
            report["repackaged"].append((base, old, new))
            continue

        def change(base=base, new=new, n=n):
            bump(base, n[1], n[2])
            recipes = dict(distro.lock().get("recipes", {}))
            recipes[base] = dict(recipes[base], version=new)
            distro.write_lock(recipes=recipes)

        log(f"{base}: {old} → {new}")
        if apply(base, change, report, f"{old} → {new}"):
            report["arch"].append((base, old, new))


def nvchecker_entries():
    """nvchecker settings per recipe: strings imported from Arch (distro.lock), or tables in distro.toml."""
    out = {}
    for base, e in distro.packages().items():
        conf = e.get("nvchecker")
        if isinstance(conf, str):
            try:
                tables = [v for v in tomllib.loads(conf).values() if isinstance(v, dict) and "source" in v]
            except tomllib.TOMLDecodeError:
                continue
            conf = tables[0] if tables else None
        if isinstance(conf, dict) and FORBIDDEN_HOST not in json.dumps(conf):
            out[base] = conf
    return out


def usable_sources(entries, report):
    """Leave out entries whose nvchecker source plugin cannot be loaded (like htmlparser without python-lxml),
    since one such entry makes nvchecker fail for all of them."""
    ok = {}
    for src in sorted({c["source"] for c in entries.values()}):
        r = subprocess.run([sys.executable, "-c", f"import nvchecker_source.{src}"], capture_output=True, text=True)
        ok[src] = r.returncode == 0
        if not ok[src]:
            names = sorted(b for b, c in entries.items() if c["source"] == src)
            err = (r.stderr.strip().splitlines() or ["cannot be loaded"])[-1]
            report["failed"].append((", ".join(names), f"nvchecker source {src}", err))
    return {b: c for b, c in entries.items() if ok[c["source"]]}


def run_nvchecker(entries):
    with tempfile.TemporaryDirectory() as tmp:
        lines = ["[__config__]", f"max_concurrency = {NVCHECKER_CONCURRENCY}"]
        token = os.environ.get("GITHUB_TOKEN")
        if token:
            with open(os.path.join(tmp, "keys.toml"), "w") as f:
                f.write(f"[keys]\ngithub = {distro.value(token)}\n")
            lines.append(f"keyfile = {distro.value(os.path.join(tmp, 'keys.toml'))}")
        for base, conf in sorted(entries.items()):
            lines.append(f"\n[{distro.key(base)}]")
            lines += [f"{distro.key(k)} = {distro.value(v)}" for k, v in conf.items()]
        cfg = os.path.join(tmp, "nvchecker.toml")
        with open(cfg, "w") as f:
            f.write("\n".join(lines) + "\n")
        log(f"running nvchecker for {len(entries)} recipes")
        r = subprocess.run(["nvchecker", "-c", cfg, "--logger", "json"], capture_output=True, text=True, timeout=3600)
    out = {}
    for line in r.stdout.splitlines():
        try:
            e = json.loads(line)
        except ValueError:
            continue
        if e.get("event") == "updated" and e.get("name") and e.get("version"):
            out[e["name"]] = e["version"]
    return out


def bump(base, new, pkgrel="1"):
    """Move a recipe to a new upstream version and regenerate its checksums from the upstream sources."""
    d = os.path.join(TOP, "packages", base)
    path = os.path.join(d, "PKGBUILD")
    text = open(path).read()
    if re.search(r"^_(commit|tag|rev)=", text, re.M):
        raise ManualUpdate("the recipe pins a commit")
    srcinfo = open(os.path.join(d, ".SRCINFO")).read()
    if FORBIDDEN_HOST in srcinfo:
        raise ManualUpdate(f"sources on {FORBIDDEN_HOST}")
    text = re.sub(r"^pkgver=.*$", f"pkgver={new}", text, count=1, flags=re.M)
    text = re.sub(r"^pkgrel=.*$", f'pkgrel="{pkgrel}"', text, count=1, flags=re.M)
    with open(path, "w") as f:
        f.write(text)
    with tempfile.TemporaryDirectory() as tmp:
        conf = os.path.join(tmp, "makepkg.conf")
        with open(conf, "w") as f:
            f.write(open("/etc/makepkg.conf").read() + f'\nCARCH="i686"\nCHOST="i686-pc-linux-gnu"\nSRCDEST="{tmp}"\n')
        r = subprocess.run(["updpkgsums"], cwd=d, capture_output=True, text=True, env=dict(os.environ, MAKEPKG_CONF=conf))
        if r.returncode != 0:
            raise RuntimeError(f"updpkgsums: {((r.stderr or r.stdout).strip().splitlines() or ['failed'])[-1]}")
    info = importpkg.srcinfo(d)
    forms = {new, new.replace(".", "_"), new.replace(".", "-"), new.replace(".", "")}
    if not any(re.match(r"\s*source(_\w+)? = .*://", l) and any(f in l for f in forms) for l in info.splitlines()):
        raise ManualUpdate(f"no source URL contains {new}")


def upstream_updates(report, only, apply_bumps):
    pinned = {b for b, t in distro.tables().items() if "version" in t} | distro.skipped()
    entries = {b: c for b, c in nvchecker_entries().items() if (not only or b in only) and b not in pinned}
    if not entries or not shutil.which("nvchecker"):
        return
    entries = usable_sources(entries, report)
    try:
        upstream = run_nvchecker(entries)
    except subprocess.TimeoutExpired as e:
        report["failed"].append(("nvchecker", "upstream versions", str(e)))
        return
    pkgs = distro.packages()
    for base in sorted(entries):
        new, e = upstream.get(base), pkgs.get(base, {})
        if not new:
            continue
        if e.get("source") == "sources":
            m = re.search(r"^pkgver=['\"]?([^'\"\s#]+)", open(os.path.join(TOP, "packages", base, "PKGBUILD")).read(), re.M)
            old = m.group(1) if m else ""
            if not old or vercmp(new, old) <= 0:
                continue
            if not apply_bumps:
                report["upstream"].append((base, old, new))
                continue
            log(f"{base}: {old} → {new} (upstream)")
            if apply(base, lambda base=base, new=new: bump(base, new), report, f"{old} → {new}"):
                report["bumped"].append((base, old, new))
        elif e.get("version") not in (None, "main", "latest"):
            old = importpkg.split_version(e["version"])[1]
            if vercmp(new, old) > 0:
                report["upstream"].append((base, old, new))


def refresh_lock(report):
    """Refresh the archlinux32 package databases and the buildroot lock, like "make lock" with REFRESH=1."""
    before = {n: p["version"] for n, p in distro.lock().get("buildroot", {}).items()}
    env = dict(os.environ, REFRESH="1")
    os.makedirs(os.path.join(TOP, "build"), exist_ok=True)
    seed = os.path.join(TOP, "build", "seed.txt")
    try:
        with open(seed, "w") as f:
            subprocess.run([sys.executable, "scripts/mkpkgs.py", "seed"], cwd=TOP, env=env, stdout=f, check=True)
        subprocess.run([sys.executable, "scripts/mkpkgs.py", "libs"], cwd=TOP, env=env, check=True)
        subprocess.run([sys.executable, "scripts/pkg.py", "lock", "buildroot", seed], cwd=TOP, env=env, check=True)
    except subprocess.CalledProcessError as e:
        report["failed"].append(("archlinux32", "buildroot lock", str(e)))
        return
    distro._cache.clear()
    after = {n: p["version"] for n, p in distro.lock().get("buildroot", {}).items()}
    for n in sorted(set(before) | set(after)):
        if before.get(n) != after.get(n):
            report["lock"].append((n, before.get(n, "—"), after.get(n, "—")))


def table(rows, head):
    out = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    out += ["| " + " | ".join(str(c).replace("|", "\\|") for c in r) + " |" for r in rows]
    return "\n".join(out) + "\n"


def write_report(report):
    parts = ["Automatic package updates. Nothing has been built: build and test before merging.\n"]
    sections = [
        ("arch", "Updated to the version in Arch Linux (pkgver bumped, previous Arch packaging kept)", ["Package", "Old", "New"]),
        ("bumped", "Updated to the latest upstream release (Tiny Hat recipes)", ["Package", "Old", "New"]),
        ("held", "Held back by the [image] deny list", ["Package", "Update", "Would pull in"]),
        ("manual", "Needs a manual update, left unchanged", ["Package", "Update", "Reason"]),
        ("failed", "Failed, left unchanged", ["Package", "Step", "Error"]),
        ("repackaged", "Repackaged by Arch Linux with the same upstream version (not changed)", ["Package", "Current", "Arch"]),
        ("upstream", "Newer upstream releases, not packaged by Arch yet (not changed)", ["Package", "Current", "Upstream"]),
        ("lock", "archlinux32 buildroot lock", ["Package", "Old", "New"]),
    ]
    for key, title, head in sections:
        if report[key]:
            parts.append(f"## {title}\n\n" + table(report[key], head))
    if len(parts) == 1:
        parts.append("Everything is up to date.\n")
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w") as f:
        f.write("\n".join(parts))
    log(f"report written to {os.path.relpath(REPORT, TOP)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("packages", nargs="*", help="only check these recipes")
    ap.add_argument("--no-arch", action="store_true", help="do not check the Arch Linux package databases")
    ap.add_argument("--no-upstream", action="store_true", help="do not run nvchecker")
    ap.add_argument("--no-bump", action="store_true", help="only report new upstream releases of Tiny Hat recipes")
    ap.add_argument("--no-lock", action="store_true", help="do not refresh the archlinux32 lock")
    args = ap.parse_args()
    only = set(args.packages)
    report = {k: [] for k in ("arch", "bumped", "held", "manual", "failed", "repackaged", "upstream", "lock")}
    try:
        STATE.append(image_state())
    except SystemExit as e:
        sys.exit(f"autoupdate: the image does not resolve before updating, so updates cannot be checked: {e}")
    if not args.no_arch:
        arch_updates(report, only)
    if not args.no_upstream:
        upstream_updates(report, only, not args.no_bump)
    if not args.no_lock and not only:
        refresh_lock(report)
    write_report(report)


if __name__ == "__main__":
    main()
