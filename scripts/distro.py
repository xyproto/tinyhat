#!/usr/bin/env python3
import json
import os
import sys
import tomllib

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISTRO = os.path.join(TOP, "distro.toml")
LOCK = os.path.join(TOP, "distro.lock")
SECTIONS = ["image", "image-aarch64", "buildroot", "providers", "mirrors", "sources"]
FACTS = ("version", "a32", "nvchecker")
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
    recipes, tabs = lock().get("recipes", {}), tables()
    return {base: {"source": "arch", **recipes.get(base, {}), **tabs.get(base, {})}
            for base in sorted(set(tabs) | set(recipes))}


def section(name):
    return load().get(name, {})


def providers():
    return dict(section("providers"))


def pins():
    return {base: [t["pin"], f"{base}-{t['pin']}-{t.get('pin_arch', 'i686')}.pkg.tar.zst", t["pin_sha256"]]
            for base, t in tables().items() if "pin" in t}


def localdeps():
    return {base: t["localdeps"] for base, t in tables().items() if "localdeps" in t}


def prebuilt_listed():
    return [base for base, t in tables().items() if t.get("prebuilt")]


def sources():
    return {name: (s["sha256"], s["url"]) for name, s in section("sources").items()}


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
        return f"image = {value(section('image'))}\nproviders = {value(providers())}\n"
    sys.exit(f"distro.py: unknown view {name}")


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
    else:
        sys.exit("usage: distro.py view theme|image OUT | lockfile buildroot OUT | source BASE | mirrors | sources | pins")


if __name__ == "__main__":
    main()
