#!/usr/bin/env python3
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import tomllib
import urllib.parse
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import distro

os.umask(0o022)

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GITLAB = os.environ.get("ARCH_GITLAB", "https://gitlab.archlinux.org/archlinux/packaging/packages")
API = f"{re.match(r'https?://[^/]+', GITLAB).group(0)}/api/v4"
A32 = os.environ.get("A32_PACKAGES", "https://git.archlinux32.org/archlinux32/packages.git")
CACHE = os.path.join(TOP, "build", "cache", "a32packages")
PKGCONF = distro.DISTRO
OVERRIDES = os.path.join(TOP, "config", "pkgbuild-overrides")
DROP = {".SRCINFO", "REUSE.toml", ".gitignore", ".gitlab-ci.yml", "LICENSES", "keys"}
NV_CHECKER = ".nvchecker.toml"
GROUP = urllib.parse.urlparse(GITLAB).path.strip("/")


def api_project(base):
    return f"{API}/projects/{urllib.parse.quote(f'{GROUP}/{project_path(base)}', safe='')}"


def die(msg):
    sys.exit(f"importpkg.py: {msg}")


def project_path(name):
    name = re.sub(r"([a-zA-Z0-9]+)\+([a-zA-Z]+)", r"\1-\2", name)
    name = name.replace("+", "plus")
    name = re.sub(r"[^a-zA-Z0-9_\-.]", "-", name)
    name = re.sub(r"[_\-]{2,}", "-", name)
    return "unix-tree" if name == "tree" else name


def split_version(version):
    epoch, _, rest = version.rpartition(":")
    pkgver, pkgrel = rest.rsplit("-", 1)
    return epoch, pkgver, pkgrel


def upstream_tag(version):
    epoch, pkgver, pkgrel = split_version(version)
    rel = pkgrel.split(".")[0]
    return f"{epoch}-{pkgver}-{rel}" if epoch else f"{pkgver}-{rel}"


def fetch_upstream(base, version, tag=None):
    path = project_path(base)
    tag = tag or upstream_tag(version)
    url = f"{GITLAB}/{path}/-/archive/{tag}/{path}-{tag}.tar.gz"
    r = subprocess.run(["curl", "-sSL", "--retry", "3", "--connect-timeout", "30", "--max-time", "300",
                        "-o", "-", "-w", "\n%{http_code}", url], capture_output=True)
    if r.returncode != 0:
        die(f"{url}: {r.stderr.decode().strip()}")
    data, _, code = r.stdout.rpartition(b"\n")
    if code in (b"404", b"302") or (code == b"200" and not data.startswith(b"\x1f\x8b")):
        return None
    if code != b"200":
        die(f"{url}: HTTP {code.decode()}")
    files = {}
    with tarfile.open(fileobj=io.BytesIO(data)) as t:
        for m in t:
            rel = m.name.split("/", 1)[-1] if "/" in m.name else ""
            if not rel or rel.split("/")[0] in DROP:
                continue
            if m.isfile():
                files[rel] = (t.extractfile(m).read(), m.mode & 0o111)
            elif m.issym():
                files[rel] = (None, m.linkname)
    return files


def a32_repo():
    if not os.path.isdir(os.path.join(CACHE, ".git")):
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        subprocess.run(["git", "clone", "-q", A32, CACHE], check=True)
    return CACHE


def git(*args):
    return subprocess.run(["git", "-C", a32_repo(), *args], check=True, capture_output=True, text=True).stdout


def a32_commit(builddate):
    return git("rev-list", "-1", f"--before={builddate}", "origin/HEAD").strip()


def fetch_overlay(base, commit):
    if commit == "-":
        return {}
    files = {}
    for repo in ("core", "extra", "build-support"):
        prefix = f"{repo}/{base}/"
        listing = git("ls-tree", "-r", "--full-tree", commit, prefix)
        for line in listing.splitlines():
            meta, path = line.split("\t", 1)
            mode, kind, obj = meta.split()
            if kind != "blob":
                continue
            data = subprocess.run(["git", "-C", a32_repo(), "cat-file", "blob", obj], check=True, capture_output=True).stdout
            rel = path[len(prefix):]
            if mode == "120000":
                files[rel] = (None, data.decode())
            else:
                files[rel] = (data, 0o111 if mode == "100755" else 0)
        if files:
            break
    return files


def packages():
    return distro.packages()


DEFAULT_BUILD_FOR = ["i686", "x32"]


def build_for(pkgs):
    return {name: e["build_for"] for name, e in pkgs.items() if "build_for" in e}


key = distro.key
fmt_value = distro.value


def fields_for(e):
    out = [("source", fmt_value(e["source"]))]
    for k, v in e.items():
        if k != "source":
            out.append((k, fmt_value(v)))
    return out


key_motif = r"(?:\"([^\"]+)\"|([^\][ \n]+))"


def split_blocks(text):
    pre = re.match(r"(?s).*?(?=^\[)", text, re.M)
    pre = pre.group(0) if pre else ""
    names, blocks = [], []
    for m in re.finditer(rf"^\[{key_motif}\]\n(?:(?!\[).*(?:\n|\Z))*", text[len(pre):], re.M):
        names.append(m.group(1) or m.group(2))
        blocks.append(m.group(0).rstrip("\n") + "\n")
    return pre, names, blocks


def write_blocks(pre, kept):
    order = lambda nb: (0, distro.SECTIONS.index(nb[0])) if nb[0] in distro.SECTIONS else (1, nb[0])
    distro.write_if_changed(PKGCONF, pre + "\n".join(b for n, b in sorted(kept, key=order)))


def upsert(base, fields):
    table = dict(distro.tables().get(base, {}))
    recipe = dict(distro.lock().get("recipes", {}).get(base, {}))
    changed = False
    for k, v in fields:
        v = tomllib.loads(f"{k} = {v}")[k]
        if k in distro.FACTS and k not in table:
            recipe[k] = v
        elif table.get(k, "arch" if k == "source" else None) != v:
            table[k] = v
            changed = True
    if recipe:
        recipes = dict(distro.lock().get("recipes", {}))
        recipes[base] = recipe
        distro.write_lock(recipes=recipes)
    if changed:
        pre, names, blocks = split_blocks(open(PKGCONF).read())
        kept = [(n, b) for n, b in zip(names, blocks) if n != base]
        kept.append((base, f"[{key(base)}]\n" + "".join(f"{k} = {fmt_value(v)}\n" for k, v in table.items())))
        write_blocks(pre, kept)


def assign_nvcheckers(map):
    recipes = {n: dict(r) for n, r in distro.lock().get("recipes", {}).items()}
    for n, text in map.items():
        recipes.setdefault(n, {})["nvchecker"] = text
    distro.write_lock(recipes=recipes)


def mangle(text, pkgrel, base, targets):
    def arch(m):
        items = m.group(2).replace("'", " ").replace('"', " ").split()
        if "any" in items:
            return f"{m.group(1)}arch=(any)"
        return f"{m.group(1)}arch=({' '.join(targets.get(base, DEFAULT_BUILD_FOR))})"

    text = re.sub(r"^(\s*)arch=\(([^)\n]*)\)[^\n]*", arch, text, flags=re.M)
    text = mangle_depends(text)
    if pkgrel is not None:
        text = re.sub(r"^(\s*pkgrel=)['\"]?[0-9]+(\.[0-9.]*)?['\"]?\s*(#.*)?$", lambda m: f'{m.group(1)}"{pkgrel}"', text, flags=re.M)
    return text


def array_end(text, start):
    i, quote = start, None
    while i < len(text):
        c = text[i]
        if quote:
            if c == quote:
                quote = None
        elif c in "'\"":
            quote = c
        elif c == "#" and text[i - 1] in " \t\n(":
            nl = text.find("\n", i)
            i = len(text) if nl < 0 else nl
            continue
        elif c == ")":
            return i + 1
        i += 1
    return len(text)


def mangle_depends(text):
    out, pos = [], 0
    for m in re.finditer(r"^\s*(make|check|opt|)depends(_[^=\s]+)?=\(", text, re.M):
        if m.start() < pos:
            continue
        end = array_end(text, m.end())
        body = text[m.end() - 1:end]
        body = re.sub(r"(['\"(\s])lib32-", r"\1", body)
        body = re.sub(r"(['\"(\s])gcc-multilib(?=['\")\s])", r"\1gcc", body)
        body = re.sub(r"((?:^|['\"(\s])[^\s$'\"()]+[<=>]+[^\s'\"()-]+)-[^:'\")\s]+(?=[:'\")\s])", r"\1", body)
        out.append(text[pos:m.end() - 1] + body)
        pos = end
    out.append(text[pos:])
    return "".join(out)


def write_tree(dest, files):
    shutil.rmtree(dest, ignore_errors=True)
    for rel, (data, extra) in sorted(files.items()):
        path = os.path.join(dest, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if data is None:
            os.symlink(extra, path)
        else:
            with open(path, "wb") as f:
                f.write(data)
            os.chmod(path, 0o755 if extra else 0o644)


def srcinfo(dest):
    with tempfile.NamedTemporaryFile("w", suffix=".conf", delete=False) as conf:
        conf.write('CARCH="i686"\nCHOST="i686-pc-linux-gnu"\nPKGEXT=".pkg.tar.zst"\nSRCEXT=".src.tar.gz"\n')
    try:
        out = subprocess.run(["makepkg", "--config", conf.name, "--printsrcinfo"], cwd=dest, check=True,
                             capture_output=True, text=True, env=dict(os.environ, LC_ALL="C")).stdout
    finally:
        os.unlink(conf.name)
    with open(os.path.join(dest, ".SRCINFO"), "w") as f:
        f.write(out)
    return out


def check_version(base, version, info):
    epoch, pkgver, pkgrel = split_version(version)
    got = {}
    for line in info.splitlines():
        if line.startswith("\t") and " = " in line:
            k, _, v = line.strip().partition(" = ")
            if k in ("pkgver", "pkgrel", "epoch") and k not in got:
                got[k] = v
    want = {"pkgver": pkgver, "pkgrel": pkgrel, "epoch": epoch}
    if any(got.get(k, "") != v for k, v in want.items()):
        print(f"  warning: {base}: PKGBUILD gives {got}, wanted {version}", file=sys.stderr)


def pkgbuild_version(text):
    def get(k, required=True):
        m = re.search(rf"^{k}=([^\s#]+)", text, re.M)
        if not m and required:
            die(f"cannot determine {k} from PKGBUILD")
        return m and m.group(1).strip("'\"")

    epoch = get("epoch", False)
    return f"{epoch + ':' if epoch else ''}{get('pkgver')}-{get('pkgrel')}"


def latest_version(base):
    url = f"{api_project(base)}/repository/tags?per_page=1"
    r = subprocess.run(["curl", "-sSL", "--retry", "3", "--connect-timeout", "30", "--max-time", "60", url],
                       capture_output=True, text=True)
    if r.returncode != 0:
        die(f"{url}: {r.stderr.strip()}")
    try:
        tags = json.loads(r.stdout)
    except ValueError:
        tags = []
    if not tags:
        die(f"{base}: no upstream tags")
    upstream = fetch_upstream(base, "", tags[0]["name"])
    if upstream is None or "PKGBUILD" not in upstream:
        die(f"{base}: no archive for upstream tag {tags[0]['name']}")
    return pkgbuild_version(upstream["PKGBUILD"][0].decode()), upstream, tags[0]["name"]


def import_one(base, entry, src, targets):
    if src != "arch":
        die(f"{base}: importing packages from source '{src}' is not implemented yet")
    version = entry.get("version", "latest")
    tag = None
    upstream = None
    if version == "latest":
        version, upstream, tag = latest_version(base)
    elif version == "main":
        tag = "main"
    else:
        tag = upstream_tag(version)
        upstream = fetch_upstream(base, version)
    commit = entry.get("a32", "-")
    overlay = fetch_overlay(base, commit)
    if upstream is None and "PKGBUILD" in overlay and not re.search(rb"^pkg(name|base)=", overlay["PKGBUILD"][0], re.M):
        tag = "main"
        upstream = fetch_upstream(base, version, tag)
    if upstream is None and "PKGBUILD" not in overlay:
        die(f"{base} {version}: no upstream tag {tag} and no archlinux32 PKGBUILD")
    files = dict(upstream or {})
    pkgbuild = b""
    if upstream is not None:
        pkgbuild = files.pop("PKGBUILD")[0] + b"\n"
        if "LICENSE" in files and b"LICENSE" not in pkgbuild:
            del files["LICENSE"]
    if "PKGBUILD" in overlay:
        pkgbuild += overlay.pop("PKGBUILD")[0] + b"\n"
    override = os.path.join(OVERRIDES, base)
    if os.path.exists(override):
        with open(override, "rb") as f:
            pkgbuild += f.read() + b"\n"
    pkgbuild += b"unset -f pkgver\n"
    files.update(overlay)
    nvc = files.pop(NV_CHECKER, None)
    rel = None if version == "main" else split_version(version)[2]
    files["PKGBUILD"] = (mangle(pkgbuild.decode(), rel, base, targets).encode(), 0)
    dest = os.path.join(TOP, "packages", base)
    write_tree(dest, files)
    info = srcinfo(dest)
    if version != "main":
        check_version(base, version, info)
    if nvc is not None and nvc[0]:
        e = dict(entry)
        e["nvchecker"] = nvc[0].decode("utf-8", "replace")
        upsert(base, fields_for(e))
    print(f"{base} {version} ({'upstream ' + tag if upstream is not None else 'archlinux32 only'}, overlay {commit[:12]})",
          file=sys.stderr)


def entries():
    out = []
    skipped = distro.skipped()
    for base, e in sorted(packages().items()):
        src = e.get("source", "arch")
        if src == "sources" or not e.get("version") or base in skipped:
            continue
        out.append((base, e, src))
    return out


def backfill_nvcheckers():
    jobs = [(base, e) for base, e in sorted(packages().items())
            if e.get("source") == "arch" and e.get("version")
            and e.get("version") not in ("main", "latest") and "nvchecker" not in e]

    def get(job):
        base, e = job
        ref = urllib.parse.quote(upstream_tag(e["version"]), safe="")
        url = f"{api_project(base)}/repository/files/{urllib.parse.quote(NV_CHECKER, safe='')}/raw?ref={ref}"
        r = subprocess.run(["curl", "-sSLf", "--retry", "2", "--connect-timeout", "30", url],
                           capture_output=True, text=True)
        return base, r.stdout if r.returncode == 0 and r.stdout.strip() else None

    found = {}
    with ThreadPoolExecutor(8) as ex:
        for base, text in ex.map(get, jobs):
            if text:
                found[base] = text
    if found:
        assign_nvcheckers(found)
    print(f"nvchecker: {len(jobs)} checked, {len(found)} recorded", file=sys.stderr)


def main():
    args = sys.argv[1:]
    if not args:
        die("usage: importpkg.py all | BASE... | add BASE VERSION [BUILDDATE] | update BASE... | nvchecker | srcinfo DIR...")
    if args[0] == "srcinfo":
        for d in args[1:]:
            srcinfo(d)
        return
    if args[0] == "nvchecker":
        backfill_nvcheckers()
        return
    if args[0] == "add":
        base, version = args[1:3]
        builddate = args[3] if len(args) > 3 else None
        if os.path.isdir(os.path.join(TOP, "packages", base)):
            die(f"packages/{base} already exists")
        fields = [("source", fmt_value("arch")), ("version", fmt_value(version))]
        if builddate is not None:
            git("fetch", "-q", "origin")
            fields.append(("a32", fmt_value(a32_commit(f"@{builddate}"))))
        upsert(base, fields)
        pkgs = packages()
        import_one(base, pkgs[base], pkgs[base].get("source", "arch"), build_for(pkgs))
        return
    if args[0] == "update":
        for base in args[1:]:
            e = packages().get(base)
            if e is None or e.get("source") != "arch" or not e.get("version"):
                die(f"{base}: not an imported Arch recipe")
            if "version" in distro.tables().get(base, {}):
                print(f"{base}: version is pinned in distro.toml", file=sys.stderr)
                continue
            upsert(base, [("version", fmt_value(latest_version(base)[0]))])
            import_one(base, packages()[base], "arch", build_for(packages()))
        return
    targets = build_for(packages())
    wanted = None if args == ["all"] else set(args)
    failed = []
    for base, e, src in entries():
        if wanted is None or base in wanted:
            try:
                import_one(base, e, src, targets)
            except (subprocess.CalledProcessError, SystemExit, ValueError, KeyError) as err:
                print(f"  error: {base}: {err}", file=sys.stderr)
                failed.append(base)
    if failed:
        die("failed: " + " ".join(failed))


if __name__ == "__main__":
    main()
