#!/usr/bin/env python3
import difflib
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
MAKEFISH = os.path.join(TOP, "packages", "makefish", "root", "usr", "bin", "makefish")
MAKEFISH_CONF = os.path.join(TOP, "packages", "makefish", "root", "etc", "makefish.conf")
PKGCONF = distro.DISTRO
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


def packages():
    return distro.packages()


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
    entry = dict(distro.lock().get("imports", {}).get(base, {}))
    changed = False
    for k, v in fields:
        v = tomllib.loads(f"{k} = {v}")[k]
        if k in distro.FACTS and k not in table:
            entry[k] = v
        elif table.get(k, "arch" if k == "source" else None) != v:
            table[k] = v
            changed = True
    if entry:
        imports = dict(distro.lock().get("imports", {}))
        imports[base] = entry
        distro.write_lock(imports=imports)
    if changed:
        pre, names, blocks = split_blocks(open(PKGCONF).read())
        kept = [(n, b) for n, b in zip(names, blocks) if n != base]
        kept.append((base, f"[{key(base)}]\n" + "".join(f"{k} = {fmt_value(v)}\n" for k, v in table.items())))
        write_blocks(pre, kept)


def assign_nvcheckers(map):
    imports = {n: dict(r) for n, r in distro.lock().get("imports", {}).items()}
    for n, text in map.items():
        imports.setdefault(n, {})["nvchecker"] = text
    distro.write_lock(imports=imports)


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
    """Write DEST/.SRCINFO from DEST/TINYBUILD, and return it."""
    out = subprocess.run(["fish", MAKEFISH, "-C", dest, "--srcinfo"], check=True, capture_output=True, text=True,
                         env=dict(os.environ, LC_ALL="C", MAKEFISH_CONF=MAKEFISH_CONF)).stdout
    with open(os.path.join(dest, ".SRCINFO"), "w") as f:
        f.write(out)
    return out


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


def bump_tinybuild(base, pkgver, pkgrel="1"):
    """Set pkgver and pkgrel in packages/BASE/TINYBUILD and write the checksums of the new sources into it."""
    d = os.path.join(TOP, "packages", base)
    path = os.path.join(d, "TINYBUILD")
    text = open(path).read()
    for k, v in (("pkgver", pkgver), ("pkgrel", pkgrel)):
        text, n = re.subn(rf"^set {k} .*$", f"set {k} {v}", text, count=1, flags=re.M)
        if n != 1:
            raise ValueError(f"{base}: no top-level 'set {k}' line in the TINYBUILD")
    with open(path, "w") as f:
        f.write(text)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["fish", MAKEFISH, "-C", d, "-s", tmp, "--updsums"], check=True,
                       env=dict(os.environ, MAKEFISH_CONF=MAKEFISH_CONF))
    return srcinfo(d)


def _without_version(text):
    """A PKGBUILD without its pkgver, pkgrel and checksum lines, to see whether anything else changed."""
    text = re.sub(r"^\s*(pkgver|pkgrel)=.*$", "", text, flags=re.M)
    out, pos = [], 0
    for m in re.finditer(r"^\s*(md5|sha1|sha224|sha256|sha384|sha512|b2|ck)sums(_\w+)?=\(", text, re.M):
        if m.start() < pos:
            continue
        out.append(text[pos:m.start()])
        pos = array_end(text, m.end())
    out.append(text[pos:])
    return re.sub(r"\n\s*\n+", "\n", "".join(out))


def ported_diff(base, new):
    """For an Arch PKGBUILD ported to a TINYBUILD: when Arch only changed the version and checksums since the version
    it was ported from, bump the TINYBUILD; otherwise print Arch's changes, to port by hand."""
    old = distro.lock().get("imports", {}).get(base, {}).get("version")
    if not old or old == new:
        print(f"{base}: TINYBUILD, already ported from Arch {new}", file=sys.stderr)
        return
    before, after = fetch_upstream(base, old) or {}, fetch_upstream(base, new) or {}
    a = before.pop("PKGBUILD", (b"", 0))[0].decode(errors="replace")
    b = after.pop("PKGBUILD", (b"", 0))[0].decode(errors="replace")
    o, n = split_version(old), split_version(new)
    if a and b and o[0] == n[0] and before == after and _without_version(a) == _without_version(b):
        bump_tinybuild(base, n[1], n[2])
        imports = dict(distro.lock().get("imports", {}))
        imports[base] = dict(imports[base], version=new)
        distro.write_lock(imports=imports)
        print(f"{base}: TINYBUILD bumped from Arch {old} to {new} (only the version and checksums changed)",
              file=sys.stderr)
        return
    print(f"{base}: TINYBUILD ported from Arch {old}; Arch has {new}. Port these changes by hand:", file=sys.stderr)
    sys.stdout.writelines(difflib.unified_diff(a.splitlines(True), b.splitlines(True), f"PKGBUILD {old}", f"PKGBUILD {new}"))
    for rel in sorted(set(before) | set(after)):
        if before.get(rel) != after.get(rel):
            print(f"  {rel} changed too", file=sys.stderr)


def import_one(base, entry, src):
    """A package directory with a TINYBUILD is bumped or gets Arch's changes printed (ported_diff). A new one gets
    Arch's files for the version, as the starting point for writing its TINYBUILD."""
    if os.path.isfile(os.path.join(TOP, "packages", base, "TINYBUILD")):
        version = entry.get("version", "latest")
        ported_diff(base, latest_version(base)[0] if version == "latest" else version)
        return
    if src != "arch":
        die(f"{base}: importing packages from source '{src}' is not implemented yet")
    dest = os.path.join(TOP, "packages", base)
    if os.path.exists(dest):
        die(f"packages/{base} exists, but has no TINYBUILD")
    version = entry.get("version", "latest")
    if version == "latest":
        version, files, tag = latest_version(base)
    else:
        tag = "main" if version == "main" else upstream_tag(version)
        files = fetch_upstream(base, version, tag)
    if files is None or "PKGBUILD" not in files:
        die(f"{base} {version}: no Arch PKGBUILD for tag {tag}")
    nvc = files.pop(NV_CHECKER, None)
    write_tree(dest, files)
    if nvc is not None and nvc[0]:
        e = dict(entry)
        e["nvchecker"] = nvc[0].decode("utf-8", "replace")
        upsert(base, fields_for(e))
    print(f"packages/{base}: Arch's files for {version}. Write packages/{base}/TINYBUILD from the PKGBUILD "
          f"(with 'set flags arch'), then delete the PKGBUILD", file=sys.stderr)


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
        die("usage: importpkg.py all | BASE... | add BASE VERSION | update BASE... | nvchecker | srcinfo DIR...")
    if args[0] == "srcinfo":
        for d in args[1:]:
            srcinfo(d)
        return
    if args[0] == "nvchecker":
        backfill_nvcheckers()
        return
    if args[0] == "add":
        base, version = args[1:3]
        if os.path.isdir(os.path.join(TOP, "packages", base)):
            die(f"packages/{base} already exists")
        upsert(base, [("source", fmt_value("arch")), ("version", fmt_value(version))])
        import_one(base, packages()[base], "arch")
        return
    if args[0] == "update":
        for base in args[1:]:
            e = packages().get(base)
            if e is None or e.get("source") != "arch" or not e.get("version"):
                die(f"{base}: not an imported Arch PKGBUILD")
            if "version" in distro.tables().get(base, {}):
                print(f"{base}: version is pinned in distro.toml", file=sys.stderr)
                continue
            if not os.path.isfile(os.path.join(TOP, "packages", base, "TINYBUILD")):
                die(f"{base}: no TINYBUILD")
            ported_diff(base, latest_version(base)[0])
        return
    wanted = None if args == ["all"] else set(args)
    failed = []
    for base, e, src in entries():
        if wanted is None or base in wanted:
            try:
                import_one(base, e, src)
            except (subprocess.CalledProcessError, SystemExit, ValueError, KeyError) as err:
                print(f"  error: {base}: {err}", file=sys.stderr)
                failed.append(base)
    if failed:
        die("failed: " + " ".join(failed))


if __name__ == "__main__":
    main()
