#!/usr/bin/env python3
import hashlib
import os
import sys
import tarfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkg

os.umask(0o022)

LISTS = {"pkgname": "NAME", "pkgver": "VERSION", "pkgbase": "BASE", "pkgdesc": "DESC", "url": "URL",
         "arch": "ARCH", "builddate": "BUILDDATE", "packager": "PACKAGER", "size": "SIZE"}
MULTI = {"license": "LICENSE", "replaces": "REPLACES", "depend": "DEPENDS", "optdepend": "OPTDEPENDS",
         "conflict": "CONFLICTS", "provides": "PROVIDES", "group": "GROUPS"}


def main(lockfile, pkgdir, root, listfile):
    explicit = set(pkg.read_list(listfile))
    local = os.path.join(root, "var/lib/pacman/local")
    os.makedirs(local, exist_ok=True)
    with open(os.path.join(local, "ALPM_DB_VERSION"), "w") as f:
        f.write("9\n")
    now = str(int(time.time()))
    for p in pkg.read_lock(lockfile):
        single, multi, backup, files, md5 = {}, {}, [], [], {}
        with tarfile.open(os.path.join(pkgdir, p["filename"])) as t:
            for m in t:
                if m.name == ".PKGINFO":
                    for line in t.extractfile(m).read().decode().splitlines():
                        if " = " not in line or line.startswith("#"):
                            continue
                        k, v = line.split(" = ", 1)
                        if k in LISTS:
                            single[LISTS[k]] = v
                        elif k in MULTI:
                            multi.setdefault(MULTI[k], []).append(v)
                        elif k == "backup":
                            backup.append(v)
                elif not m.name.startswith("."):
                    if m.isfile() and m.name.startswith("etc/"):
                        md5[m.name] = hashlib.md5(t.extractfile(m).read()).hexdigest()
                    files.append(m.name + ("/" if m.isdir() and not m.name.endswith("/") else ""))
        d = os.path.join(local, f"{p['name']}-{p['version']}")
        os.makedirs(d, exist_ok=True)
        out = []
        for key in ("NAME", "VERSION", "BASE", "DESC", "URL", "ARCH", "BUILDDATE"):
            if key in single:
                out.append(f"%{key}%\n{single[key]}\n")
        out.append(f"%INSTALLDATE%\n{now}\n")
        for key in ("PACKAGER", "SIZE"):
            if key in single:
                out.append(f"%{key}%\n{single[key]}\n")
        if p["name"] not in explicit:
            out.append("%REASON%\n1\n")
        for key in ("GROUPS", "LICENSE", "REPLACES", "DEPENDS", "OPTDEPENDS", "CONFLICTS", "PROVIDES"):
            if key in multi:
                out.append(f"%{key}%\n" + "\n".join(multi[key]) + "\n")
        out.append("%VALIDATION%\nsha256\n")
        with open(os.path.join(d, "desc"), "w") as f:
            f.write("\n".join(out) + "\n")
        with open(os.path.join(d, "files"), "w") as f:
            f.write("%FILES%\n" + "\n".join(sorted(files)) + "\n\n")
            if backup:
                f.write("%BACKUP%\n" + "\n".join(f"{b}\t{md5.get(b, '')}" for b in backup) + "\n\n")


if __name__ == "__main__":
    main(*sys.argv[1:5])
