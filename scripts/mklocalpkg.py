#!/usr/bin/env python3
import os
import shutil
import sys
import time

os.umask(0o022)


def remove_existing(root, name):
    local = os.path.join(root, "var/lib/pacman/local")
    for entry in os.listdir(local):
        desc = os.path.join(local, entry, "desc")
        if os.path.exists(desc):
            with open(desc) as f:
                lines = f.read().split("\n")
            if lines[:2] == ["%NAME%", name]:
                shutil.rmtree(os.path.join(local, entry))


def main(root, out, name, version, desc, url, lic, provides=""):
    files = []
    for d, dirs, names in os.walk(out):
        for n in dirs + names:
            rel = os.path.relpath(os.path.join(d, n), out)
            if os.path.lexists(os.path.join(root, rel)):
                files.append(rel + ("/" if n in dirs else ""))
    size = sum(os.lstat(os.path.join(root, f)).st_size for f in files if not f.endswith("/"))
    now = str(int(time.time()))
    remove_existing(root, name)
    d = os.path.join(root, "var/lib/pacman/local", f"{name}-{version}")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "desc"), "w") as f:
        f.write(f"%NAME%\n{name}\n\n%VERSION%\n{version}\n\n%DESC%\n{desc}\n\n%URL%\n{url}\n\n%ARCH%\ni686\n\n"
                f"%BUILDDATE%\n{now}\n\n%INSTALLDATE%\n{now}\n\n%PACKAGER%\nTiny Hat\n\n%SIZE%\n{size}\n\n"
                f"%LICENSE%\n{lic}\n\n%VALIDATION%\nnone\n\n")
        if provides:
            f.write("%PROVIDES%\n" + "\n".join(provides.split(",")) + "\n\n")
    with open(os.path.join(d, "files"), "w") as f:
        f.write("%FILES%\n" + "\n".join(sorted(files)) + "\n\n")


if __name__ == "__main__":
    main(*sys.argv[1:9])
