#!/usr/bin/env python3
import os
import re
import shutil
import sys
import tarfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkg

os.umask(0o022)

TOP = pkg.TOP
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(TOP, "build", "licenses")
LOCK = sys.argv[1] if len(sys.argv) > 1 else os.path.join(TOP, "build", "rootfs.lock")
ARCH = sys.argv[3] if len(sys.argv) > 3 else "i686"
KERNEL_ARCH = sys.argv[4] if len(sys.argv) > 4 else ARCH
REPO = os.path.join(TOP, "build", "repo")
PKGDIR = os.path.join(TOP, "build", f"repo-{ARCH}") if ARCH in ("aarch64", "x86_64") else REPO

BOOT = ["tinyhat-mountroot", "memtest86+", "tinyhat-splash", "grub", "amd-ucode", "intel-ucode", "grub-efi64"]


def info(path, name, version, url, lic):
    with open(os.path.join(path, "LICENSE-INFO"), "w") as f:
        f.write(f"name: {name}\nversion: {version}\nurl: {url}\nlicense: {lic}\n")


def spdx_ids(expr):
    return [t for t in re.split(r"[\s()]+", expr) if t and t not in ("AND", "OR", "WITH")]


def main():
    spdx = {}
    files = pkg.arch_files(REPO, ARCH)
    with tarfile.open(pkg.arch_files(REPO, "i686")["licenses"]) as t:
        for m in t:
            if m.isfile() and m.name.startswith("usr/share/licenses/spdx/") and m.name.endswith(".txt"):
                spdx[os.path.basename(m.name)[:-4]] = t.extractfile(m).read()

    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    used = set()

    entries = [p for p in pkg.read_lock(LOCK) if p["repo"] != "lib32"]
    for name in BOOT:
        if name in files and name not in {p["name"] for p in entries}:
            entries.append({"name": name, "version": pkg.pkginfo(files[name])["pkgver"][0], "path": files[name]})
    kernel = pkg.arch_files(REPO, KERNEL_ARCH)["linux-tinyhat"]
    entries.append({"name": "linux-tinyhat", "version": pkg.pkginfo(kernel)["pkgver"][0], "path": kernel})
    for p in entries:
        path = os.path.join(OUT, p["name"])
        os.makedirs(path)
        url, lic = "", ""
        with tarfile.open(p.get("path") or os.path.join(PKGDIR, p["filename"])) as t:
            for m in t:
                if m.isfile() and m.name.startswith("usr/share/licenses/"):
                    with open(os.path.join(path, os.path.basename(m.name)), "wb") as f:
                        f.write(t.extractfile(m).read())
                elif m.name == ".PKGINFO":
                    lines = t.extractfile(m).read().decode().splitlines()
                    url = next((l.split(" = ", 1)[1] for l in lines if l.startswith("url = ")), "")
                    lic = " AND ".join(l.split(" = ", 1)[1] for l in lines if l.startswith("license = "))
        info(path, p["name"], p["version"], url, lic)
        used.update(spdx_ids(lic))

    if ARCH != "aarch64":
        path = os.path.join(OUT, "shim")
        os.makedirs(path)
        info(path, "shim and MokManager (signed by Microsoft and Fedora)", "16.1-10", "https://github.com/rhboot/shim",
             "BSD-2-Clause AND BSD-2-Clause-Patent AND OpenSSL")
        used.update(["BSD-2-Clause", "BSD-2-Clause-Patent", "OpenSSL"])

    os.makedirs(os.path.join(OUT, "spdx"))
    for lid in sorted(used):
        if lid in spdx:
            with open(os.path.join(OUT, "spdx", lid + ".txt"), "wb") as f:
                f.write(spdx[lid])
    print(f"licenses: {len(os.listdir(OUT)) - 1} projects", file=sys.stderr)


if __name__ == "__main__":
    main()
