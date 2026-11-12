#!/usr/bin/env python3
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import distro
import pkg

TOP = pkg.TOP
REPO = os.path.join(TOP, "build", "repo")
META = [".PKGINFO", ".BUILDINFO", ".MTREE", ".INSTALL", ".CHANGELOG"]


def recipe_version(name):
    fields = {}
    with open(os.path.join(TOP, "packages", name, ".SRCINFO")) as f:
        for line in f:
            k, _, v = line.strip().partition(" = ")
            if k in ("pkgver", "pkgrel", "epoch") and k not in fields:
                fields[k] = v
    epoch = f"{fields['epoch']}:" if fields.get("epoch") else ""
    return f"{epoch}{fields['pkgver']}-{fields['pkgrel']}"


def built(name):
    path = pkg.arch_files(REPO, "i686").get(name)
    if path and pkg.pkginfo(path)["pkgver"][0] == recipe_version(name):
        return path
    return None


def main():
    root = sys.argv[1]
    conf = distro.section("buildroot")
    names = conf.get("local", [])
    subprocess.run([sys.executable, os.path.join(TOP, "scripts", "srcinfo.py")], check=True)
    excludes = [a for p in META + conf.get("exclude", []) for a in ("--exclude", p)]
    done = []
    for name in names:
        path = built(name)
        if path is None:
            log = os.path.join(TOP, "build", "log", f"i686-{name}.log")
            os.makedirs(os.path.dirname(log), exist_ok=True)
            print(f"localroot.py: building {name} for the build environment", file=sys.stderr)
            with open(log, "w") as out:
                r = subprocess.run([os.path.join(TOP, "scripts", "makepkg.sh"), os.path.join(TOP, "packages", name)]
                                   + [f"i686/{d}" for d in done], stdout=out, stderr=subprocess.STDOUT, cwd=TOP)
            if r.returncode != 0:
                with open(log) as f:
                    sys.stderr.write("".join(f.readlines()[-40:]))
                sys.exit(f"localroot.py: building {name} failed, see {log}")
            path = built(name)
            if path is None:
                sys.exit(f"localroot.py: {name} {recipe_version(name)} is not in {REPO} after building it")
            stamp = os.path.join(TOP, "build", "pkg", "i686", name)
            os.makedirs(os.path.dirname(stamp), exist_ok=True)
            with open(stamp, "a"):
                os.utime(stamp)
        subprocess.run(["bsdtar", "--no-xattrs", "--no-acls", "--no-fflags", "-xf", path, "-C", root] + excludes,
                       check=True)
        done.append(name)
    subprocess.run(["ldconfig", "-r", root], check=True)


if __name__ == "__main__":
    main()
