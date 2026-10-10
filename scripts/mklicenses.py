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
    ("orbiton", "main (80679da9)", "https://github.com/xyproto/orbiton", "BSD-3-Clause",
     "orbiton-80679da9d30712b9a5ba21071bbdc9adb1c9ec03.tar.gz", ["LICENSE"]),
    ("grafx2", "master (f84cb09d) ported to SDL3", "http://grafx2.eu/", "GPL-2.0-only",
     "grafx2-f84cb09dc59d706d6e7b28778b01ba911f52298c.tar.gz", ["LICENSE", "doc/gpl-2.0.txt"]),
    ("lua-5.4.9-grafx2", "5.4.9 (built into Grafx2)", "https://www.lua.org/", "MIT",
     "lua-5.4.9.tar.gz", ["doc/readme.html"]),
    ("recoil", "6.4.5 (built into Grafx2)", "https://recoil.sourceforge.net/", "GPL-2.0-or-later",
     "recoil-6.4.5.tar.gz", ["COPYING"]),
    ("6502", "0.1 (built into Grafx2)", "https://github.com/redcode/6502", "GPL-3.0-or-later",
     "6502-v0.1.tar.xz", ["COPYING"]),
    ("libtonc", "1.4.3 (git c5af1b2)", "https://github.com/gbadev-org/libtonc", "MIT",
     "libtonc-c5af1b2cb019dcde43216596390490bc07800b21.tar.gz", ["license.txt"]),
    ("gbafix", "1.2.0 (from gba-tools)", "https://github.com/devkitPro/gba-tools", "LGPL-2.0-or-later",
     "gba-tools-v1.2.0.tar.gz", ["COPYING", "src/gbafix.c"]),
    ("rfxgen", "5.0", "https://github.com/raysan5/rfxgen", "Zlib",
     "rfxgen-5.0.tar.gz", ["LICENSE"]),
    ("mgba", "0.10.5", "https://mgba.io/", "MPL-2.0",
     "mgba-0.10.5.tar.gz", ["LICENSE"]),
    ("vtgbte", "master (1a9f4603)", "https://github.com/paul-arutyunov/vtGBte", "MIT",
     "vtGBte-1a9f460390f0e8c7ab32dd7df0f125ad23d405a9.tar.gz", ["LICENSE"]),
    ("blastem", "libretro master (1e0de94d)", "https://github.com/libretro/blastem", "GPL-3.0-or-later AND BSD-3-Clause AND Zlib",
     "blastem-1e0de94dc7e669c0925a22c0fccf6cdc837af0a0.tar.gz", ["COPYING", "libchdr/LICENSE.txt", "zlib/LICENSE"]),
    ("vice", "3.9", "https://vice-emu.sourceforge.io/", "GPL-2.0-or-later",
     "vice-3.9.tar.gz", ["COPYING", "README"]),
    ("tic80", "1.3.1", "https://github.com/nesbox/TIC-80", "MIT",
     "tic80-1.3.1.tar.gz", ["LICENSE"]),
    ("tic80-argparse", "0d5f5d07 (bundled with TIC-80)", "https://github.com/cofyc/argparse", "MIT",
     "tic80-argparse-0d5f5d07.tar.gz", ["LICENSE"]),
    ("tic80-blip-buf", "330226d9 (bundled with TIC-80)", "https://github.com/nesbox/blip-buf", "LGPL-2.1-or-later",
     "tic80-blip-buf-330226d9.tar.gz", ["license.txt"]),
    ("tic80-giflib", "1aa11b06 (bundled with TIC-80)", "https://github.com/nesbox/giflib", "MIT",
     "tic80-giflib-1aa11b06.tar.gz", ["COPYING"]),
    ("tic80-jsmn", "25647e69 (bundled with TIC-80)", "https://github.com/zserge/jsmn", "MIT",
     "tic80-jsmn-25647e69.tar.gz", ["LICENSE"]),
    ("tic80-libpng", "ed217e3e (bundled with TIC-80)", "https://github.com/glennrp/libpng", "Libpng",
     "tic80-libpng-ed217e3e.tar.gz", ["LICENSE"]),
    ("tic80-lua", "75ea9ccb (bundled with TIC-80)", "https://github.com/lua/lua", "MIT",
     "tic80-lua-75ea9ccb.tar.gz", ["lua.h"]),
    ("tic80-naett", "10a96244 (bundled with TIC-80)", "https://github.com/erkkah/naett", "MIT",
     "tic80-naett-10a96244.tar.gz", ["LICENSE"]),
    ("tic80-zip", "296ff242 (bundled with TIC-80)", "https://github.com/kuba--/zip", "Unlicense",
     "tic80-zip-296ff242.tar.gz", ["LICENSE.txt"]),
    ("tic80-zlib", "51b7f2ab (bundled with TIC-80)", "https://github.com/madler/zlib", "Zlib",
     "tic80-zlib-51b7f2ab.tar.gz", ["LICENSE"]),
    ("furnace", "0.6.8.3", "https://github.com/tildearrow/furnace", "GPL-2.0-or-later",
     "furnace-0.6.8.3.tar.gz", ["LICENSE"]),
    ("furnace-fmt", "bundled with Furnace", "https://github.com/fmtlib/fmt", "MIT",
     "furnace-fmt-e57ca2e3.tar.gz", ["LICENSE.rst"]),
    ("furnace-adpcm", "bundled with Furnace", "https://github.com/superctr/adpcm", "MIT",
     "furnace-adpcm-ef7a2171.tar.gz", ["LICENSE"]),
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

    path = os.path.join(OUT, "beneath-a-steel-sky")
    os.makedirs(path)
    with zipfile.ZipFile(os.path.join(SRC, "BASS-Floppy-1.3.zip")) as z:
        with open(os.path.join(path, "LICENSE"), "wb") as f:
            f.write(z.read("readme.txt"))
    info(path, "Beneath a Steel Sky (floppy)", "1.3", "https://www.scummvm.org/games/#games-sky", "LicenseRef-BASS-Freeware")

    for name, title, version, zipname, member, url in (
            ("lure-of-the-temptress", "Lure of the Temptress", "1.1", "lure-1.1.zip", "lure/LICENSE.txt", "https://www.scummvm.org/games/#games-lure"),
            ("soltys", "Soltys", "1.0", "soltys-en-v1.0.zip", "license.txt", "https://www.scummvm.org/games/#games-soltys"),
            ("nippon-safes", "Nippon Safes Inc.", "1.0", "nippon-1.0.zip", "readme.txt", "https://www.scummvm.org/games/#games-nippon")):
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
