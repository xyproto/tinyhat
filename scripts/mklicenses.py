#!/usr/bin/env python3
import os
import re
import shutil
import sys
import tarfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkg

os.umask(0o022)

TOP = pkg.TOP
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(TOP, "build", "licenses")
LOCK = sys.argv[1] if len(sys.argv) > 1 else os.path.join(TOP, "build", "rootfs.lock")
PKGDIR = os.path.join(TOP, "build", "repo")
SRC = os.path.join(TOP, "vendor", "src")

SOURCES = [
    ("linux", "7.3", "https://www.kernel.org/", "GPL-2.0-only WITH Linux-syscall-note",
     "linux-7.3.tar.xz", ["COPYING", "LICENSES/preferred/GPL-2.0", "LICENSES/exceptions/Linux-syscall-note"]),
    ("xpadneo", "0.10.4", "https://github.com/atar-axis/xpadneo", "GPL-2.0-only",
     "xpadneo-0.10.4.tar.gz", ["LICENSE.md", "LICENSES/"]),
    ("open-watcom", "2.0 (2026-10-01 build)", "https://github.com/open-watcom/open-watcom-v2", "Watcom-1.0",
     "open-watcom-2026-10-01.tar.xz", ["license.txt"]),
    ("pixelitos", "20260721 (icon theme)", "https://github.com/ItzSelenux/pixelitos-icon-theme", "MIT",
     "pixelitos-icon-theme-20260721.tar.gz", ["LICENSE", "CREDITS.md"]),
]
BOOT = ["busybox", "memtest86+", "tinyhat-splash", "grub", "amd-ucode", "intel-ucode", "grub-efi64"]

def info(path, name, version, url, lic):
    with open(os.path.join(path, "LICENSE-INFO"), "w") as f:
        f.write(f"name: {name}\nversion: {version}\nurl: {url}\nlicense: {lic}\n")


def spdx_ids(expr):
    return [t for t in re.split(r"[\s()]+", expr) if t and t not in ("AND", "OR", "WITH")]


def main():
    spdx = {}
    files = pkg.arch_files(PKGDIR, "i686")
    with tarfile.open(files["licenses"]) as t:
        for m in t:
            if m.isfile() and m.name.startswith("usr/share/licenses/spdx/") and m.name.endswith(".txt"):
                spdx[os.path.basename(m.name)[:-4]] = t.extractfile(m).read()

    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    used = set()

    entries = [p for p in pkg.read_lock(LOCK) if p["repo"] != "lib32"]
    for name in BOOT:
        if name in files and name not in {p["name"] for p in entries}:
            entries.append({"name": name, "version": pkg.pkginfo(files[name])["pkgver"][0],
                            "filename": os.path.basename(files[name])})
    for p in entries:
        path = os.path.join(OUT, p["name"])
        os.makedirs(path)
        with tarfile.open(os.path.join(PKGDIR, p["filename"])) as t:
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

    for name, version, url, lic, tarball, files in SOURCES:
        path = os.path.join(OUT, name)
        os.makedirs(path)
        with tarfile.open(os.path.join(SRC, tarball)) as t:
            for m in t:
                rel = m.name.split("/", 1)[-1]
                if m.isfile() and (rel in files or any(f.endswith("/") and rel.startswith(f) for f in files)):
                    with open(os.path.join(path, os.path.basename(rel)), "wb") as f:
                        f.write(t.extractfile(m).read())
        info(path, name, version, url, lic)
        used.update(spdx_ids(lic))

    path = os.path.join(OUT, "shim")
    os.makedirs(path)
    info(path, "shim and MokManager (signed by Microsoft and Fedora)", "16.1-10", "https://github.com/rhboot/shim",
         "BSD-2-Clause AND BSD-2-Clause-Patent AND OpenSSL")
    used.update(["BSD-2-Clause", "BSD-2-Clause-Patent", "OpenSSL"])

    path = os.path.join(OUT, "generaluser-gs")
    os.makedirs(path)
    shutil.copy(os.path.join(SRC, "GeneralUser-GS-LICENSE.txt"), os.path.join(path, "LICENSE"))
    with open(os.path.join(path, "LICENSE"), "a") as f:
        f.write("\nTiny Hat ships GeneralUser GS 2.0.3 by S. Christian Collins converted to SF3 (Ogg Vorbis),\n"
                "with samples normalized and the instrument attenuation adjusted to keep the original balance.\n")
    info(path, "GeneralUser GS", "2.0.3", "https://schristiancollins.com/generaluser.php", "LicenseRef-GeneralUser-GS")

    path = os.path.join(OUT, "beneath-a-steel-sky")
    os.makedirs(path)
    with zipfile.ZipFile(os.path.join(SRC, "BASS-Floppy-1.3.zip")) as z:
        with open(os.path.join(path, "LICENSE"), "wb") as f:
            f.write(z.read("readme.txt"))
    info(path, "Beneath a Steel Sky (floppy)", "1.3", "https://www.scummvm.org/games/#games-sky", "LicenseRef-BASS-Freeware")

    for name, title, version, zipname, member, url in (
            ("lure-of-the-temptress", "Lure of the Temptress", "1.1", "lure-1.1.zip", "lure/LICENSE.txt", "https://www.scummvm.org/games/#games-lure"),
            ("soltys", "Soltys", "1.0", "soltys-en-v1.0.zip", "license.txt", "https://www.scummvm.org/games/#games-soltys")):
        path = os.path.join(OUT, name)
        os.makedirs(path)
        with zipfile.ZipFile(os.path.join(SRC, zipname)) as z:
            with open(os.path.join(path, "LICENSE"), "wb") as f:
                f.write(z.read(member))
        info(path, title, version, url, "LicenseRef-" + name + "-Freeware")

    for name, title, zipname in (("beej-bgc", "Beej's Guide to C Programming", "beej-bgc.zip"),
                                 ("beej-bgclr", "Beej's Guide to the C Library", "beej-bgclr.zip")):
        path = os.path.join(OUT, name)
        os.makedirs(path)
        with open(os.path.join(path, "LICENSE"), "w") as f:
            f.write(f"{title} by Brian \"Beej Jorgen\" Hall is licensed under the Creative Commons\n"
                    "Attribution-Noncommercial-No Derivative Works 3.0 License.\n"
                    "Tiny Hat ships the unmodified HTML edition in /usr/share/tinyhat/c-guide.\n")
        info(path, title, zipname[:-4], "https://beej.us/guide/", "CC-BY-NC-ND-3.0")
        used.add("CC-BY-NC-ND-3.0")

    os.makedirs(os.path.join(OUT, "spdx"))
    for lid in sorted(used):
        if lid in spdx:
            with open(os.path.join(OUT, "spdx", lid + ".txt"), "wb") as f:
                f.write(spdx[lid])
    print(f"licenses: {len(entries) + len(SOURCES) + 1} projects", file=sys.stderr)


if __name__ == "__main__":
    main()
