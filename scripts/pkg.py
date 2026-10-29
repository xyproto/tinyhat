#!/usr/bin/env python3
import fcntl
import fnmatch
import hashlib
import os
import re
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.parse
import urllib.request

import distro

os.umask(0o022)

MIRROR = os.environ.get("MIRROR", "https://mirror.archlinux32.org/i686")
ARCHIVE = os.environ.get("ARCHIVE", "https://archive.archlinux32.org/packages")
MIRRORS = os.environ.get("MIRRORS", "https://archlinux32.andreasbaumann.cc/i686 "
                         f"https://mirror.math.princeton.edu/pub/archlinux32/i686 {MIRROR}").split()
REPOS = ["core", "extra"]
TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DBDIR = os.path.join(TOP, "vendor", "db")
VENDORDIR = os.path.join(TOP, "vendor", "archlinux32")
# Prebuilt packages for the other architectures. The x86 images use archlinux32 (above)
ARCH_SOURCES = {
    "aarch64": {
        "mirrors": os.environ.get("ALARM_MIRRORS", "http://de.mirror.archlinuxarm.org/aarch64 "
                                  "http://dk.mirror.archlinuxarm.org/aarch64").split(),
        "repos": ["core", "extra", "alarm"],
        "dbdir": os.path.join(TOP, "vendor", "db", "aarch64"),
        "vendordir": os.path.join(TOP, "vendor", "archlinuxarm"),
    },
}


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


def download(url, dest, tries=4):
    tmp = f"{dest}.part.{os.getpid()}"
    print(f"  get {url}", file=sys.stderr)
    req = urllib.request.Request(url, headers={"User-Agent": "tinyhat-build/1.0"})
    try:
        for attempt in range(tries):
            try:
                with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
                    while chunk := r.read(1 << 20):
                        f.write(chunk)
                break
            except urllib.error.HTTPError:
                raise
            except OSError as e:
                if attempt == tries - 1:
                    raise
                print(f"  retrying {url}: {e}", file=sys.stderr)
        os.replace(tmp, dest)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def download_any(urls, dest):
    """Download from the first of urls that works."""
    for i, url in enumerate(urls):
        try:
            return download(url, dest, tries=2)
        except OSError as e:
            if i == len(urls) - 1:
                raise
            print(f"  {url}: {e}", file=sys.stderr)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def strip_ver(dep):
    return re.split(r"[<>=]", dep, maxsplit=1)[0]


def parse_desc(text):
    """The fields of a repository database desc (or depends) file, as lists of lines."""
    fields, key = {}, None
    for line in text.splitlines():
        if line.startswith("%") and line.endswith("%"):
            key = line[1:-1]
            fields[key] = []
        elif line and key:
            fields[key].append(line)
    return fields


def parse_pkginfo(text):
    info = {}
    for line in text.splitlines():
        if " = " in line and not line.startswith("#"):
            k, v = line.split(" = ", 1)
            info.setdefault(k, []).append(v)
    return info


def add_pkg(pkgs, provides, p, provs):
    if p["name"] in pkgs:
        return
    pkgs[p["name"]] = p
    for pv in provs:
        provides.setdefault(strip_ver(pv), []).append(p["name"])


def load_db(dbdir=DBDIR, repos=None):
    pkgs, provides = {}, {}
    for repo in repos or REPOS:
        path = os.path.join(dbdir, f"{repo}.db")
        if not os.path.exists(path):
            die(f"missing {path}, run 'make lock'")
        # Older databases (like Arch Linux ARM's) keep %DEPENDS% and %PROVIDES% in a separate "depends" file
        entries = {}
        with tarfile.open(path) as t:
            for m in t:
                d, _, kind = m.name.rpartition("/")
                if kind not in ("desc", "depends") or not m.isfile():
                    continue
                entries.setdefault(d, {}).update(parse_desc(t.extractfile(m).read().decode()))
        for fields in entries.values():
            if "NAME" not in fields:
                continue
            add_pkg(pkgs, provides, {
                "name": fields["NAME"][0],
                "repo": repo,
                "version": fields["VERSION"][0],
                "filename": fields["FILENAME"][0],
                "sha256": fields["SHA256SUM"][0],
                "depends": [strip_ver(d) for d in fields.get("DEPENDS", [])],
            }, fields.get("PROVIDES", []))
    return pkgs, provides


def pkginfo(path):
    with tarfile.open(path) as t:
        for m in t:
            if m.name == ".PKGINFO":
                return parse_pkginfo(t.extractfile(m).read().decode())
    die(f"no .PKGINFO in {path}")


def split_filename(fn):
    m = re.match(r"^(.+)-([^-]+)-([^-]+)-([^-]+)\.pkg\.tar\.[a-z0-9]+$", fn)
    if not m:
        return None
    return m.group(1), f"{m.group(2)}-{m.group(3)}", m.group(4)


def repo_files(repodir):
    newest = {}
    if not os.path.isdir(repodir):
        return newest
    for fn in os.listdir(repodir):
        parts = split_filename(fn)
        if parts is None:
            continue
        key = (parts[0], parts[2])
        path = os.path.join(repodir, fn)
        if key not in newest or os.path.getmtime(path) > os.path.getmtime(newest[key]):
            newest[key] = path
    return newest


def arch_files(repodir, arch):
    out = {}
    for (name, a), path in repo_files(repodir).items():
        if a == arch or (a == "any" and name not in out):
            out[name] = path
    return out


def load_repo(repodir, arch="i686"):
    pkgs, provides = {}, {}
    for name, path in sorted(arch_files(repodir, arch).items()):
        info = pkginfo(path)
        add_pkg(pkgs, provides, {
            "name": info["pkgname"][0],
            "arch": info["arch"][0],
            "repo": "tinyhat",
            "version": info["pkgver"][0],
            "filename": os.path.basename(path),
            "sha256": sha256(path),
            "depends": [strip_ver(d) for d in info.get("depend", [])],
        }, info.get("provides", []))
    return pkgs, provides


def lookup(index, dep, prefs, chosen):
    pkgs, provides = index
    if dep in prefs and prefs[dep] in pkgs:
        return prefs[dep]
    if dep in pkgs:
        return dep
    if dep in provides:
        cands = sorted(provides[dep])
        return next((c for c in cands if c in chosen), cands[0])
    return None


def resolve_targets(targets, indexes, arch, prefs, skip):
    chosen, names, seen, queue = {}, set(), set(), [(t, arch) for t in targets]
    while queue:
        dep, ctx = queue.pop(0)
        if (dep, ctx) in seen or dep in skip:
            continue
        seen.add((dep, ctx))
        p = None
        for a in ([ctx, "i686"] if ctx == "x32" else [ctx]):
            name = lookup(indexes[a], dep, prefs, names)
            if name is not None:
                p = indexes[a][0][name]
                break
        if p is None:
            die(f"cannot resolve '{dep}' for {ctx}")
        if p["name"] in skip:
            continue
        key = (p["name"], p.get("arch", ctx))
        if key in chosen:
            continue
        chosen[key] = p
        names.add(p["name"])
        nctx = ctx if key[1] == "any" else key[1]
        queue.extend((d, nctx) for d in p["depends"])
    return [chosen[k] for k in sorted(chosen)]


def list_config(name, extra=()):
    s = distro.section(name)
    targets = list(s.get("packages", []))
    skip = set(s.get("skip", []))
    if name.startswith("image-"):
        base = distro.section("image")
        remove = set(s.get("remove", []))
        targets = [t for t in base.get("packages", []) if t not in remove]
        targets += [t for t in s.get("add", []) if t not in targets]
        skip |= set(base.get("skip", []))
    for path in extra:
        targets += [t for t in read_list(path) if t not in targets]
    return targets, distro.providers(), skip


def resolve(name, extra=(), repodir=None, arch="i686"):
    targets, prefs, skip = list_config(name, extra)
    if repodir:
        indexes = {a: load_repo(repodir, a) for a in ({"i686", arch} if arch == "x32" else {arch})}
    else:
        indexes = {"i686": load_db()}
    return resolve_targets(targets, indexes, arch, prefs, skip)


def lock_line(p, repo=None):
    return f"{repo or p['repo']} {p['name']} {p['version']} {p['filename']} {p['sha256']}\n"


def parse_lock_line(line):
    repo, name, ver, fn, h = line.split()
    return {"repo": repo, "name": name, "version": ver, "filename": fn, "sha256": h}


def write_lock(sel, lockfile):
    native = {p["name"] for p in sel if p.get("arch") == "x32"}
    with open(lockfile, "w") as f:
        for p in sel:
            repo = p["repo"]
            if native and p.get("arch") == "i686":
                repo = "lib32" if p["name"] in native else "i686"
            f.write(lock_line(p, repo))
    print(f"{lockfile}: {len(sel)} packages", file=sys.stderr)


def arch_db(arch):
    """The package databases for another architecture (see ARCH_SOURCES), downloaded if missing or REFRESH is set."""
    src = ARCH_SOURCES[arch]
    os.makedirs(src["dbdir"], exist_ok=True)
    for repo in src["repos"]:
        path = os.path.join(src["dbdir"], f"{repo}.db")
        if os.environ.get("REFRESH") or not os.path.exists(path):
            download_any([f"{m}/{repo}/{repo}.db" for m in src["mirrors"]], path)
    return load_db(src["dbdir"], src["repos"])


def cmd_lock(name, *extra):
    os.makedirs(DBDIR, exist_ok=True)
    if os.environ.get("REFRESH") or not all(
        os.path.exists(os.path.join(DBDIR, f"{r}.db")) for r in REPOS
    ):
        for repo in REPOS:
            download(f"{MIRROR}/{repo}/{repo}.db", os.path.join(DBDIR, f"{repo}.db"))
    sel = resolve(name, extra)
    pins = distro.pins()
    for p in sel:
        if p["name"] in pins:
            p["version"], p["filename"], p["sha256"] = pins[p["name"]]
    distro.write_lock(**{name: {p["name"]: {"repo": p["repo"], "version": p["version"], "file": p["filename"],
                                            "sha256": p["sha256"]} for p in sel}})
    print(f"distro.lock [{name}]: {len(sel)} packages", file=sys.stderr)


def cmd_local(name, lockfile, repodir, arch="i686"):
    write_lock(resolve(name, repodir=repodir, arch=arch), lockfile)


def pkgbase(path, name):
    """The pkgbase of the package file, with the name as fallback."""
    try:
        return pkginfo(path).get("pkgbase", (name,))[0]
    except (OSError, tarfile.TarError):
        return name


SEEDS = {"i686": "buildroot", "aarch64": "buildroot-aarch64"}


def seed_versions(arch):
    """The versions of the packages in the chroot seed of arch, as locked in distro.lock."""
    return {n: e.get("version") for n, e in distro.lock().get(SEEDS.get(arch, ""), {}).items()
            if isinstance(e, dict)}


def base_files(files, *names, seed=None):
    """The files of all packages with the same pkgbase as the requested names, plus the transitive closure
    of their dependencies that exist in the repo, in sorted order.

    Needed because makepkg.sh only installs the explicitly listed packages: without this, a package would
    build against prebuilt versions of its dependencies even when locally built (newer) ones are in the
    repo, breaking pkg-config chains like pango -> harfbuzz, or against stale sibling versions of split
    packages whose shared files (like the gobject-introspection .pc files) moved to other subpackages."""
    def needs(info):
        """The dependency fields of a (7.x) pkginfo, so also 'depends' of makepkg <= 6."""
        return [d for k in info for d in info[k] if k in ("depend", "depends")]
    infos, provides, bybase = {}, {}, {}
    for name, path in files.items():
        info = repo_info(path)
        infos[name] = info
        bybase.setdefault(info.get("pkgbase", (name,))[0], []).append(name)
        for pv in info.get("provides", []):
            provides.setdefault(strip_ver(pv), []).append(name)
    todo, seen = list(names), set()
    while todo:
        short = strip_ver(todo.pop())
        depname = short.split("=", 1)[0]
        if depname.startswith("/"):
            continue  # a file dependency, not a package name (the chroot seed provides it)
        if depname not in infos:
            depname = next((n for n in provides.get(depname, []) if n in infos), None)  # maybe a provide
            if depname is None:
                continue  # not in the repo: the prebuilt version in the chroot seed is fine
        if depname in seen:
            continue
        if seed and depname not in names and seed.get(depname) == repo_info(files[depname]).get("pkgver", ("",))[0]:
            continue  # the same version is already in the chroot seed
        seen.add(depname)
        todo += [d for n in bybase.get(repo_info(files[depname]).get("pkgbase", (depname,))[0], [depname])
                 for d in needs(infos[n])]
    return sorted(seen)


def repo_info(path):
    """The pkginfo of a repo package file, or a stub if unreadable."""
    try:
        return pkginfo(path)
    except (OSError, tarfile.TarError):
        stub = os.path.basename(path).rsplit("-", 3)[0]
        return {"pkgname": (stub,), "pkgbase": (stub,)}


def cmd_repofiles(repodir, arch, *names):
    files = arch_files(repodir, arch)
    for name in names:
        if name not in files:
            die(f"package {name} ({arch}) was not found in {repodir}")
    for name in base_files(files, *names, seed=seed_versions(arch)):
        print(files[name])


def read_lock(lockfile):
    with open(lockfile) as f:
        return [parse_lock_line(line) for line in f if line.strip()]


def link(src, dest):
    """Point dest at src with a relative symlink instead of storing a second copy. The sandboxes in
    makepkg.sh mount vendor/ at /vendor, so links from build/repo resolve inside them too."""
    if os.path.lexists(dest):
        os.unlink(dest)
    os.symlink(os.path.relpath(os.path.realpath(src), os.path.dirname(os.path.abspath(dest))), dest)


def fetch_pkg(p, pkgdir, arch=None):
    dest = os.path.join(pkgdir, p["filename"])
    if os.path.exists(dest) and sha256(dest) == p["sha256"]:
        return
    cached = os.path.join(TOP, "vendor", "pkg", p["filename"])
    vendordir = ARCH_SOURCES[arch]["vendordir"] if arch in ARCH_SOURCES else VENDORDIR
    vendored = os.path.join(vendordir, p["name"], p["filename"])
    for src in (cached, vendored):
        if os.path.abspath(pkgdir) == os.path.dirname(os.path.abspath(src)):
            continue
        if os.path.exists(src) and sha256(src) == p["sha256"]:
            link(src, dest)
            return
    os.makedirs(os.path.dirname(vendored), exist_ok=True)
    fn = urllib.parse.quote(p["filename"])
    if arch in ARCH_SOURCES:
        urls = [f"{m}/{p['repo']}/{fn}" for m in ARCH_SOURCES[arch]["mirrors"]]
    else:
        urls = [f"{m}/{p['repo']}/{fn}" for m in MIRRORS] + [f"{ARCHIVE}/{p['name'][0]}/{p['name']}/{fn}"]
    download_any(urls, vendored)
    if sha256(vendored) != p["sha256"]:
        os.unlink(vendored)
        die(f"checksum mismatch for {p['filename']}")
    link(vendored, dest)


def srcdest_names(srcinfo):
    """The names that downloaded sources get in SRCDEST, as makepkg's get_filename() names them."""
    out = set()
    with open(srcinfo) as f:
        for line in f:
            k, _, v = line.strip().partition(" = ")
            if not re.fullmatch(r"source(_\w+)?", k) or "://" not in v:
                continue
            if "::" in v:
                out.add(v.split("::", 1)[0])
                continue
            proto = v.split("://", 1)[0].split("+", 1)[0]
            name = v.rsplit("/", 1)[-1]
            if proto in ("bzr", "fossil", "git", "hg", "svn"):
                name = v.split("#", 1)[0].split("?", 1)[0].removesuffix("/").rsplit("/", 1)[-1]
                if proto == "bzr":
                    name = name.split("lp:", 1)[-1]
                elif proto == "fossil":
                    name += ".fossil"
                elif proto == "git":
                    name = name.split(".git", 1)[0]
            out.add(name)
    return out


def cmd_prunable(pkgdir):
    """Print the downloaded sources of one package that no other package or distro.toml [sources] uses."""
    pkgdir = os.path.abspath(pkgdir)
    mine = srcdest_names(os.path.join(pkgdir, ".SRCINFO"))
    others = set(distro.section("sources"))
    for d in os.listdir(os.path.join(TOP, "packages")):
        info = os.path.join(TOP, "packages", d, ".SRCINFO")
        if os.path.join(TOP, "packages", d) != pkgdir and os.path.exists(info):
            others |= srcdest_names(info)
    for name in sorted(mine - others):
        if name and "/" not in name and name not in (".", ".."):
            print(name)


def cmd_fetch(lockfile, pkgdir, arch=None):
    os.makedirs(pkgdir, exist_ok=True)
    for p in read_lock(lockfile):
        fetch_pkg(p, pkgdir, arch)


def current_db(max_age=6 * 3600):
    dbdir = os.path.join(DBDIR, "current")
    os.makedirs(dbdir, exist_ok=True)
    with open(os.path.join(dbdir, ".lock"), "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for repo in REPOS:
            path = os.path.join(dbdir, f"{repo}.db")
            if os.path.exists(path) and time.time() - os.path.getmtime(path) < max_age:
                continue
            download_any([f"{m}/{repo}/{repo}.db" for m in MIRRORS], path)
    return load_db(dbdir)


def cmd_archrepo(arch, repodir, lockfile):
    """Fill repodir with the prebuilt packages of [image-ARCH] and the packages built for ARCH that the image
    uses ([build-ARCH] in distro.lock), and write the lock file for the root file system of that image."""
    os.makedirs(repodir, exist_ok=True)
    sel = []
    for e in distro.locked(f"image-{arch}"):
        p = {"repo": e["repo"], "name": e["name"], "version": e["version"], "filename": e["file"], "sha256": e["sha256"]}
        fetch_pkg(p, repodir, arch)
        sel.append(p)
    built = arch_files(os.path.join(TOP, "build", "repo"), arch)
    for names in distro.lock().get(f"build-{arch}", {}).values():
        for name in names:
            if name not in built:
                die(f"{name} ({arch}) has not been built")
            fn = os.path.basename(built[name])
            link(built[name], os.path.join(repodir, fn))
            sel.append({"repo": "tinyhat", "name": name, "version": pkginfo(built[name])["pkgver"][0],
                        "filename": fn, "sha256": sha256(built[name])})
    write_lock(sorted(sel, key=lambda p: p["name"]), lockfile)


def cmd_prebuilt(repodir, lockfile, *names):
    os.makedirs(repodir, exist_ok=True)
    os.makedirs(os.path.dirname(lockfile), exist_ok=True)
    db = current_db()[0]
    pins = distro.pins()
    with open(lockfile, "a+") as lf:
        fcntl.flock(lf, fcntl.LOCK_EX)
        lf.seek(0)
        locked = {p["name"]: p for p in (parse_lock_line(line) for line in lf if line.strip())}
        entries = dict(locked)
        for name in names:
            if name in locked:
                p = dict(locked[name])
            elif name in db:
                p = entries[name] = dict(db[name])
            else:
                print(f"  prebuilt: {name}: not found", file=sys.stderr)
                continue
            if name in pins:
                p["version"], p["filename"], p["sha256"] = pins[name]
            fetch_pkg(p, repodir)
        lf.seek(0)
        lf.truncate()
        lf.writelines(lock_line(entries[n]) for n in sorted(entries))


def cmd_source(srcdir, *names):
    src = distro.sources()
    os.makedirs(srcdir, exist_ok=True)
    for name in names or src:
        if name not in src:
            die(f"{name} is not listed in distro.toml [sources]")
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
    order = {"i686": 1, "lib32": 2}
    for p in sorted(read_lock(lockfile), key=lambda p: order.get(p["repo"], 0)):
        path = os.path.join(pkgdir, p["filename"])
        if p["repo"] in order:
            extract_lib32(path, root, p["repo"] == "lib32", pats)
            continue
        subprocess.run(["bsdtar", "--no-xattrs", "--no-acls", "--no-fflags", "-xf", path, "-C", root] + args, check=True)


def extract_lib32(path, root, libs_only, excludes):
    with tarfile.open(path) as t:
        for m in t:
            name = m.name
            if any(fnmatch.fnmatch(name, pat) or fnmatch.fnmatch(name, pat + "/*") for pat in excludes):
                continue
            lib = re.fullmatch(r"usr/lib/[^/]+\.so[^/]*", name) and name != "usr/lib/ld-linux.so.2"
            if libs_only and not lib and name != "usr/lib/ld-linux.so.2":
                continue
            if lib:
                m.name = "usr/lib32/" + name[len("usr/lib/"):]
            if m.islnk() and re.fullmatch(r"usr/lib/[^/]+\.so[^/]*", m.linkname):
                m.linkname = "usr/lib32/" + m.linkname[len("usr/lib/"):]
            dest = os.path.join(root, m.name)
            if m.isdir() and os.path.isdir(dest):
                continue
            if os.path.lexists(dest) and not os.path.isdir(dest):
                os.unlink(dest)
            t.extract(m, root, numeric_owner=True, filter="fully_trusted")


def main():
    if len(sys.argv) < 2:
        die("usage: pkg.py lock SECTION [LIST...] | local SECTION LOCK REPODIR [ARCH] | repofiles REPODIR ARCH NAME... | "
            "fetch LOCK DIR [ARCH] | prebuilt REPODIR LOCK NAME... | archrepo ARCH REPODIR LOCK | extract LOCK DIR ROOT [SECTION] | source DIR [NAME...] | prunable PKGDIR")
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "lock":
        cmd_lock(*args)
    elif cmd == "local":
        cmd_local(*args)
    elif cmd == "repofiles":
        cmd_repofiles(*args)
    elif cmd == "fetch":
        cmd_fetch(*args)
    elif cmd == "prebuilt":
        cmd_prebuilt(*args)
    elif cmd == "archrepo":
        cmd_archrepo(*args)
    elif cmd == "source":
        cmd_source(*args)
    elif cmd == "prunable":
        cmd_prunable(*args)
    elif cmd == "extract":
        ex = []
        if len(args) > 3:
            ex = distro.section(args[3]).get("exclude", [])
            if args[3].startswith("image-"):
                ex = distro.section("image").get("exclude", []) + ex
        cmd_extract(args[0], args[1], args[2], ex)
    else:
        die(f"unknown command {cmd}")


if __name__ == "__main__":
    main()
