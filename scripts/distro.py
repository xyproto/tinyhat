#!/usr/bin/env python3
import json
import os
import re
import shlex
import sys
import tomllib

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISTRO = os.path.join(TOP, "distro.toml")
LOCK = os.path.join(TOP, "distro.lock")
SECTIONS = ["image", "image-orbiton", "image-x32", "image-aarch64", "image-x86_64", "buildroot", "providers", "mirrors", "sources"]
FACTS = ("version",)
THEME = ("name", "hat_color", "hat_highlight_color")

_cache = {}


def _load(path):
    st = os.stat(path) if os.path.exists(path) else None
    key = (path, st and st.st_mtime_ns)
    if key not in _cache:
        if st is None:
            _cache[key] = {}
        else:
            with open(path, "rb") as f:
                _cache[key] = tomllib.load(f)
    return _cache[key]


def load():
    return _load(DISTRO)


def lock():
    return _load(LOCK)


def tables():
    return {k: v for k, v in load().items() if isinstance(v, dict) and k not in SECTIONS}


def packages():
    imports, tabs = lock().get("imports", {}), tables()
    return {base: {"source": "arch", **imports.get(base, {}), **tabs.get(base, {})}
            for base in sorted(set(tabs) | set(imports))}


NVCHECKER_LINE = re.compile(r"^set nvchecker .*$", re.M)
FISH_BARE = re.compile(r"[A-Za-z0-9_.,:/@+=-]+")


def fish_words(text):
    """The words of a fish command line, with fish's quoting and escapes undone."""
    words, i, n = [], 0, len(text)
    while i < n:
        while i < n and text[i] in " \t":
            i += 1
        if i >= n or text[i] == "#":
            break
        w = []
        while i < n and text[i] not in " \t":
            c = text[i]
            if c == "'":
                i += 1
                while i < n and text[i] != "'":
                    if text[i] == "\\" and i + 1 < n and text[i + 1] in "\\'":
                        i += 1
                    w.append(text[i])
                    i += 1
                i += 1
            elif c == '"':
                i += 1
                while i < n and text[i] != '"':
                    if text[i] == "\\" and i + 1 < n and text[i + 1] in '\\"$':
                        i += 1
                    w.append(text[i])
                    i += 1
                i += 1
            elif c == "\\" and i + 1 < n:
                w.append(text[i + 1])
                i += 2
            else:
                w.append(c)
                i += 1
        words.append("".join(w))
    return words


def fish_quote(word):
    if FISH_BARE.fullmatch(word):
        return word
    out = []
    for i, c in enumerate(word):
        if c == "'" or (c == "\\" and (i + 1 == len(word) or word[i + 1] in "\\'")):
            out.append("\\")
        out.append(c)
    return "'" + "".join(out) + "'"


def tinybuild_path(base):
    return os.path.join(TOP, "packages", base, "TINYBUILD")


def nvchecker(base):
    """The nvchecker settings of a package directory, from 'set nvchecker key=value...' in its TINYBUILD, or None."""
    path = tinybuild_path(base)
    if not os.path.exists(path):
        return None
    with open(path) as f:
        m = NVCHECKER_LINE.search(f.read())
    if not m:
        return None
    out = {}
    for word in fish_words(m.group(0))[2:]:
        k, sep, v = word.partition("=")
        if sep:
            out[k] = {"true": True, "false": False}.get(v, v)
    return out or None


def nvchecker_line(table):
    items = sorted(table.items(), key=lambda kv: kv[0] != "source")
    return "set nvchecker " + " ".join(fish_quote(f"{k}={str(v).lower() if isinstance(v, bool) else v}") for k, v in items)


def set_nvchecker(base, table):
    """Write the nvchecker settings into the TINYBUILD of a package directory, after its 'set url' line."""
    path = tinybuild_path(base)
    with open(path) as f:
        text = f.read()
    line = nvchecker_line(table)
    if NVCHECKER_LINE.search(text):
        new = NVCHECKER_LINE.sub(lambda m: line, text, count=1)
    else:
        for anchor in ("url", "pkgdesc", "pkgver"):
            m = re.search(rf"^set {anchor} .*$", text, re.M)
            if m:
                break
        if not m:
            raise ValueError(f"{path}: no 'set url' line to put the nvchecker settings after")
        new = text[:m.end()] + "\n" + line + text[m.end():]
    write_if_changed(path, new)


def nvchecker_from_toml(text):
    """The first table of an nvchecker TOML file (like Arch's .nvchecker.toml), or None."""
    tabs = [v for v in tomllib.loads(text).values() if isinstance(v, dict) and "source" in v]
    return tabs[0] if tabs else None


def plan():
    """build/plan.json, the build plan that scripts/build_planner.py mk writes, or None before the first make."""
    path = os.path.join(TOP, "build", "plan.json")
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return json.load(f)


def section(name):
    return load().get(name, {})


def providers():
    return dict(section("providers"))


def unignored():
    return sorted(base for base, t in tables().items() if t.get("pacman_ignore") is False)


def pins():
    return {base: [t["pin"], f"{base}-{t['pin']}-{t.get('pin_arch', 'i686')}.pkg.tar.zst", t["pin_sha256"]]
            for base, t in tables().items() if "pin" in t}


def skipped():
    return {base for base, t in tables().items() if t.get("skip_for_now")}


def excluded():
    return set(section("deny").get("everywhere", []))


def deny_image():
    return list(section("deny").get("image", []))


def deny_files():
    return list(section("deny").get("files", []))


def sources():
    return {name: (s["sha256"], s["url"]) for name, s in section("sources").items()}


def caps(root):
    names = {}
    with open("/usr/include/linux/capability.h") as f:
        for line in f:
            w = line.split()
            if len(w) == 3 and w[0] == "#define" and w[1].startswith("CAP_") and w[2].isdigit():
                names[w[1].lower()] = int(w[2])
    out = []
    for t in tables().values():
        for path, cs in t.get("caps", {}).items():
            if os.path.isfile(os.path.join(root, path)):
                bits = sum(1 << names[c] for c in cs.split(","))
                v = (0x02000001).to_bytes(4, "little") + (bits & 0xffffffff).to_bytes(4, "little") + bytes(4) + (bits >> 32).to_bytes(4, "little") + bytes(4)
                out.append(f"/{path} x security.capability=0x{v.hex()}")
    return out


def locked(name):
    return [dict(p, name=n) for n, p in sorted(lock().get(name, {}).items())]


def key(name):
    ok = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"
    return name if name and all(c in ok for c in name) else json.dumps(name)


def value(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, dict):
        return "{ " + ", ".join(f"{key(k)} = {value(x)}" for k, x in v.items()) + " }"
    if isinstance(v, list):
        return "[" + ", ".join(value(x) for x in v) + "]"
    return json.dumps(v, ensure_ascii=False)


def write_if_changed(path, text):
    try:
        with open(path) as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass
    tmp = f"{path}.tmp.{os.getpid()}"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)
    # Writes in quick succession can keep the same mtime, so drop the cached copy explicitly
    for k in [k for k in _cache if k[0] == path]:
        del _cache[k]


def write_lock(**changes):
    data = {**lock(), **changes}
    out = []
    if "libpkgs" in data:
        out.append("libpkgs = [\n" + "".join(f"    {value(n)},\n" for n in data["libpkgs"]) + "]\n")
    for sect in sorted(k for k, v in data.items() if isinstance(v, dict)):
        if data[sect]:
            out.append(f"[{sect}]\n" + "".join(f"{key(n)} = {value(e)}\n" for n, e in sorted(data[sect].items())))
    write_if_changed(LOCK, "\n".join(out))


def lock_lines(name):
    return "".join(f"{p['repo']} {p['name']} {p['version']} {p['file']} {p['sha256']}\n" for p in locked(name))


def view(name):
    d = load()
    if name == "theme":
        return "".join(f"{k} = {value(d[k])}\n" for k in THEME if k in d)
    if name == "image":
        return f"image = {value(section('image'))}\nimage-x32 = {value(section('image-x32'))}\nproviders = {value(providers())}\n"
    sys.exit(f"distro.py: unknown view {name}")


def theme_env(base):
    """Shell exports of the theme for a package directory that sets theme = true in distro.toml, as TINYHAT_THEME_KEY."""
    if not tables().get(base, {}).get("theme"):
        return ""
    d = load()
    return "".join(f"export TINYHAT_THEME_{k.upper()}={shlex.quote(str(d[k]))}\n" for k in THEME if k in d)


def main():
    cmd, args = (sys.argv[1], sys.argv[2:]) if len(sys.argv) > 1 else ("", [])
    if cmd == "view":
        write_if_changed(args[1], view(args[0]))
    elif cmd == "lockfile":
        write_if_changed(args[1], lock_lines(args[0]))
    elif cmd == "source":
        print(packages().get(args[0], {}).get("source", "arch"))
    elif cmd == "mirrors":
        for prefix, urls in section("mirrors").items():
            print(prefix, " ".join(urls))
    elif cmd == "sources":
        for name, (h, url) in sources().items():
            print(name, h, url)
    elif cmd == "pins":
        print("\n".join(pins()))
    elif cmd == "unignored":
        print("\n".join(unignored()))
    elif cmd == "packages":
        # packages SECTION [KEY]: a list of a section, "packages" by default (like buildroot i686)
        print(" ".join(section(args[0]).get(args[1] if len(args) > 1 else "packages", [])))
    elif cmd == "local":
        print(" ".join(section(args[0]).get("local", [])))
    elif cmd == "heavy":
        print(1 if tables().get(args[0], {}).get("heavy") else 0)
    elif cmd == "theme-env":
        sys.stdout.write(theme_env(args[0]))
    elif cmd == "caps":
        print("\n".join(caps(args[0])))
    else:
        sys.exit("usage: distro.py view theme|image OUT | lockfile buildroot OUT | source BASE | mirrors | sources | pins | unignored | caps ROOT | heavy BASE | packages SECTION | local SECTION | theme-env BASE")


if __name__ == "__main__":
    main()
