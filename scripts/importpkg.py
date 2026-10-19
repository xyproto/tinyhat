#!/usr/bin/env python3
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

os.umask(0o022)

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GITLAB = os.environ.get("ARCH_GITLAB", "https://gitlab.archlinux.org/archlinux/packaging/packages")
A32 = os.environ.get("A32_PACKAGES", "https://git.archlinux32.org/archlinux32/packages.git")
CACHE = os.path.join(TOP, "build", "cache", "a32packages")
LIST = os.path.join(TOP, "config", "import.txt")
OVERRIDES = os.path.join(TOP, "config", "pkgbuild-overrides")
I686ONLY = os.path.join(TOP, "config", "i686only.txt")
DROP = {".SRCINFO", ".nvchecker.toml", "REUSE.toml", ".gitignore", ".gitlab-ci.yml", "LICENSES", "keys"}


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


def i686only():
    with open(I686ONLY) as f:
        return {line.strip() for line in f if line.strip() and not line.startswith("#")}


def mangle(text, pkgrel, base):
    def arch(m):
        items = m.group(2).replace("'", " ").replace('"', " ").split()
        if "any" in items:
            return f"{m.group(1)}arch=(any)"
        return f"{m.group(1)}arch=(i686)" if base in i686only() else f"{m.group(1)}arch=(i686 x32)"

    text = re.sub(r"^(\s*)arch=\(([^)\n]*)\)[^\n]*", arch, text, flags=re.M)
    text = mangle_depends(text)
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


def import_one(base, version, commit, subdir):
    ref = upstream_tag(version)
    upstream = fetch_upstream(base, version)
    overlay = fetch_overlay(base, commit)
    if upstream is None and "PKGBUILD" in overlay and not re.search(rb"^pkg(name|base)=", overlay["PKGBUILD"][0], re.M):
        ref = "main"
        upstream = fetch_upstream(base, version, ref)
    if upstream is None and "PKGBUILD" not in overlay:
        die(f"{base} {version}: no upstream tag {upstream_tag(version)} and no archlinux32 PKGBUILD")
    files = dict(upstream or {})
    pkgbuild = b""
    if upstream is not None:
        pkgbuild = upstream.pop("PKGBUILD")[0] + b"\n"
        files.pop("PKGBUILD", None)
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
    files["PKGBUILD"] = (mangle(pkgbuild.decode(), split_version(version)[2], base).encode(), 0)
    dest = os.path.join(TOP, "pkgbuilds", subdir, base)
    write_tree(dest, files)
    check_version(base, version, srcinfo(dest))
    print(f"{subdir}/{base} {version} ({'upstream ' + ref if upstream is not None else 'archlinux32 only'}, overlay {commit[:12]})",
          file=sys.stderr)


def read_import():
    out = []
    with open(LIST) as f:
        for line in f:
            fields = line.split()
            if fields and not fields[0].startswith("#"):
                out.append((fields + ["arch"])[:4])
    return out


def main():
    args = sys.argv[1:]
    if not args:
        die("usage: importpkg.py all | BASE... | add BASE VERSION BUILDDATE [SUBDIR] | srcinfo DIR...")
    if args[0] == "srcinfo":
        for d in args[1:]:
            srcinfo(d)
        return
    if args[0] == "add":
        base, version, builddate = args[1:4]
        subdir = args[4] if len(args) > 4 else "arch"
        git("fetch", "-q", "origin")
        commit = a32_commit(f"@{builddate}")
        entries = [e for e in read_import() if e[0] != base] + [[base, version, commit, subdir]]
        with open(LIST, "w") as f:
            for e in sorted(entries):
                f.write(" ".join(e) + "\n")
        import_one(base, version, commit, subdir)
        return
    entries = read_import()
    wanted = None if args == ["all"] else set(args)
    failed = []
    for base, version, commit, subdir in entries:
        if wanted is None or base in wanted:
            try:
                import_one(base, version, commit, subdir)
            except (subprocess.CalledProcessError, SystemExit) as e:
                print(f"  error: {base}: {e}", file=sys.stderr)
                failed.append(base)
    if failed:
        die("failed: " + " ".join(failed))


if __name__ == "__main__":
    main()
