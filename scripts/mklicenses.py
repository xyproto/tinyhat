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
OUT = os.path.join(TOP, "licenses")
PKGDIR = os.path.join(TOP, "vendor", "pkg")
SRC = os.path.join(TOP, "vendor", "src")

SOURCES = [
    ("linux", "7.2.9", "https://www.kernel.org/", "GPL-2.0-only WITH Linux-syscall-note",
     "linux-7.2.9.tar.xz", ["COPYING", "LICENSES/preferred/GPL-2.0", "LICENSES/exceptions/Linux-syscall-note"]),
    ("make", "4.4.1", "https://www.gnu.org/software/make/", "GPL-3.0-or-later",
     "make-4.4.1.tar.gz", ["COPYING"]),
    ("llvm-project", "15.0.7", "https://llvm.org/", "Apache-2.0 WITH LLVM-exception",
     "llvm-project-15.0.7.src.tar.xz", ["LICENSE.TXT"]),
    ("dosbox-x", "2026.10.01", "https://dosbox-x.com/", "GPL-2.0-or-later",
     "dosbox-x-2026.10.01.tar.gz", ["COPYING"]),
    ("zsnes", "2.3.6", "https://github.com/xyproto/zsnes", "GPL-2.0-or-later",
     "zsnes-2.3.6.tar.gz", ["COPYING"]),
    ("scummvm", "2026.3.0", "https://www.scummvm.org/", "GPL-3.0-or-later",
     "scummvm-2026.3.0.tar.xz", ["COPYING", "COPYRIGHT", "AUTHORS", "LICENSES/"]),
    ("wlroots", "0.19.3", "https://gitlab.freedesktop.org/wlroots/wlroots", "MIT",
     "wlroots-0.19.3.tar.gz", ["LICENSE"]),
    ("dwl", "0.8 with the bar patch", "https://codeberg.org/dwl/dwl", "GPL-3.0-only AND MIT AND CC0-1.0",
     "dwl-0.8.tar.gz", ["LICENSE", "LICENSE.dwm", "LICENSE.sway", "LICENSE.tinywl"]),
    ("swaylock", "1.8.6", "https://github.com/swaywm/swaylock", "MIT",
     "swaylock-1.8.6.tar.gz", ["LICENSE"]),
    ("libxml2-2.15.1", "2.15.1 without ICU", "https://gitlab.gnome.org/GNOME/libxml2", "MIT",
     "libxml2-2.15.1.tar.xz", ["Copyright"]),
    ("libxml2-2.13.9", "2.13.9 without ICU (libxml2.so.2 for LLVM)", "https://gitlab.gnome.org/GNOME/libxml2", "MIT",
     "libxml2-2.13.9.tar.xz", ["Copyright"]),
    ("orbiton", "main (f9e82501)", "https://github.com/xyproto/orbiton", "BSD-3-Clause",
     "orbiton-f9e82501b558dc8c0632e7d5909b9845c985dfcc.tar.gz", ["LICENSE"]),
    ("grafx2", "master (f84cb09d) ported to SDL3", "http://grafx2.eu/", "GPL-2.0-only",
     "grafx2-f84cb09dc59d706d6e7b28778b01ba911f52298c.tar.gz", ["LICENSE", "doc/gpl-2.0.txt"]),
    ("lua-5.4.9-grafx2", "5.4.9 (built into Grafx2)", "https://www.lua.org/", "MIT",
     "lua-5.4.9.tar.gz", ["doc/readme.html"]),
    ("recoil", "6.4.5 (built into Grafx2)", "https://recoil.sourceforge.net/", "GPL-2.0-or-later",
     "recoil-6.4.5.tar.gz", ["COPYING"]),
    ("6502", "0.1 (built into Grafx2)", "https://github.com/redcode/6502", "GPL-3.0-or-later",
     "6502-v0.1.tar.xz", ["COPYING"]),
    ("wordgrinder", "0.8", "http://cowlark.com/wordgrinder/", "MIT",
     "wordgrinder-0.8.tar.gz", ["licenses/"]),
    ("sdl2_image", "2.8.12", "https://github.com/libsdl-org/SDL_image", "Zlib",
     "SDL2_image-2.8.12.tar.gz", ["LICENSE.txt"]),
    ("sdl3_image", "3.4.8", "https://github.com/libsdl-org/SDL_image", "Zlib",
     "SDL3_image-3.4.8.tar.gz", ["LICENSE.txt"]),
    ("sdl3_ttf", "3.2.2", "https://github.com/libsdl-org/SDL_ttf", "Zlib",
     "SDL3_ttf-3.2.2.tar.gz", ["LICENSE.txt"]),
    ("xpadneo", "0.10.4", "https://github.com/atar-axis/xpadneo", "GPL-2.0-only",
     "xpadneo-0.10.4.tar.gz", ["LICENSE.md", "LICENSES/"]),
    ("fontconfig-2.18.3", "2.18.3 (replaces the archlinux32 package)", "https://www.freedesktop.org/wiki/Software/fontconfig/", "HPND AND MIT",
     "fontconfig-2.18.3.tar.xz", ["COPYING"]),
    ("cc65", "2.19 (git 71746c8)", "https://cc65.github.io/", "Zlib",
     "cc65-71746c829e77f74c2b601a7d16821170c2610df3.tar.gz", ["LICENSE"]),
    ("open-watcom", "2.0 (2026-10-01 build)", "https://github.com/open-watcom/open-watcom-v2", "Watcom-1.0",
     "open-watcom-2026-10-01.tar.xz", ["license.txt"]),
    ("gdb", "17.1", "https://www.sourceware.org/gdb/", "GPL-3.0-or-later",
     "gdb-17.1.tar.xz", ["COPYING3"]),
    ("raylib", "6.0 on SDL3", "https://www.raylib.com/", "Zlib",
     "raylib-6.0.tar.gz", ["LICENSE"]),
    ("swaybg", "1.2.2", "https://github.com/swaywm/swaybg", "MIT",
     "swaybg-1.2.2.tar.gz", ["LICENSE"]),
    ("fuzzel", "1.8.1", "https://codeberg.org/dnkl/fuzzel", "MIT",
     "fuzzel-1.8.1.tar.gz", ["LICENSE"]),
    ("libdisplay-info", "0.3.0", "https://gitlab.freedesktop.org/emersion/libdisplay-info", "MIT",
     "libdisplay-info-0.3.0.tar.gz", ["LICENSE"]),
    ("fluidsynth", "2.6.1", "https://www.fluidsynth.org/", "LGPL-2.1-or-later",
     "fluidsynth-2.6.1.tar.gz", ["LICENSE"]),
    ("sdl2-compat", "2.32.74", "https://github.com/libsdl-org/sdl2-compat", "Zlib",
     "sdl2-compat-2.32.74.tar.gz", ["LICENSE.txt"]),
    ("sdl3", "3.4.18 with the Tiny Hat Wayland framebuffer patch", "https://libsdl.org/", "Zlib",
     "SDL3-3.4.18.tar.gz", ["LICENSE.txt"]),
]

FATBOY = """TinyHat-GM 0.790 is derived from the FatBoy 0.790 GM/GS SoundFont
by Chris Wadge and other contributors (https://fatboy.site/, archived at
https://archive.org/details/fat-boy-v-0.790).

It was converted to SF3 (Ogg Vorbis compressed samples), with samples
normalized and instrument attenuation adjusted to keep the original balance.
It is renamed because the FatBoy terms below ask for modified versions to be
renamed.

The FatBoy terms, as embedded in FatBoy-v0.790.sf2:

    Use it for whatever you want, please don't sell the soundfont itself. :)

    If you wish to modify and redistribute it, please rename your soundfont
    to distinguish it from this one. Even better, share your contributions
    with me so we can make FatBoy better for everyone: https://fatboy.site/
"""


def info(path, name, version, url, lic):
    with open(os.path.join(path, "LICENSE-INFO"), "w") as f:
        f.write(f"name: {name}\nversion: {version}\nurl: {url}\nlicense: {lic}\n")


def spdx_ids(expr):
    return [t for t in re.split(r"[\s()]+", expr) if t and t not in ("AND", "OR", "WITH")]


def main():
    spdx = {}
    with tarfile.open(os.path.join(PKGDIR, pkg.read_lock(os.path.join(TOP, "licenses.lock"))[0]["filename"])) as t:
        for m in t:
            if m.isfile() and m.name.startswith("usr/share/licenses/spdx/") and m.name.endswith(".txt"):
                spdx[os.path.basename(m.name)[:-4]] = t.extractfile(m).read()

    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    used = set()

    entries = pkg.read_lock(os.path.join(TOP, "rootfs.lock")) + pkg.read_lock(os.path.join(TOP, "initramfs.lock"))
    entries += [p for p in pkg.read_lock(os.path.join(TOP, "buildroot.lock")) if p["name"] == "grub"]
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

    path = os.path.join(OUT, "tinyhat-gm")
    os.makedirs(path)
    with open(os.path.join(path, "LICENSE"), "w") as f:
        f.write(FATBOY)
    info(path, "TinyHat-GM", "0.790", "https://archive.org/details/fat-boy-v-0.790", "LicenseRef-FatBoy")


    path = os.path.join(OUT, "gcem")
    os.makedirs(path)
    with zipfile.ZipFile(os.path.join(SRC, "gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200.zip")) as z:
        with open(os.path.join(path, "LICENSE"), "wb") as f:
            f.write(z.read("gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200/LICENSE"))
    info(path, "gcem (built into fluidsynth)", "012ae73c", "https://github.com/kthohr/gcem", "Apache-2.0")
    used.add("Apache-2.0")

    path = os.path.join(OUT, "generaluser-gs")
    os.makedirs(path)
    shutil.copy(os.path.join(SRC, "GeneralUser-GS-LICENSE.txt"), os.path.join(path, "LICENSE"))
    with open(os.path.join(path, "LICENSE"), "a") as f:
        f.write("\nTiny Hat ships GeneralUser GS 2.0.3 by S. Christian Collins converted to SF3 (Ogg Vorbis),\n"
                "with samples normalized and the instrument attenuation adjusted to keep the original balance.\n")
    info(path, "GeneralUser GS", "2.0.3", "https://schristiancollins.com/generaluser.php", "LicenseRef-GeneralUser-GS")

    path = os.path.join(OUT, "flight-of-the-amazon-queen")
    os.makedirs(path)
    with zipfile.ZipFile(os.path.join(SRC, "FOTAQ_Talkie-1.1.zip")) as z:
        with open(os.path.join(path, "LICENSE"), "wb") as f:
            f.write(z.read("readme.txt"))
    info(path, "Flight of the Amazon Queen (CD talkie, MP3)", "1.1", "https://www.scummvm.org/games/#games-queen", "LicenseRef-FOTAQ-Freeware")

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
