#!/usr/bin/env python3
import os
import re
import sys
import tarfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkg

TOP = pkg.TOP
PKGBUILDS = os.path.join(TOP, "pkgbuilds")
CONFIG = os.path.join(TOP, "config")
SEED_BASE = ["base-devel", "pacman", "fakeroot", "git", "ccache", "python", "perl"]
SYSROOT = {"x32-gcc": ["glibc"]}
FILES_CACHE = os.path.join(TOP, "build", "cache", "files")


def strip_ver(dep):
    return re.split(r"[<>=]", dep, maxsplit=1)[0]


def parse(path):
    base, pkgs, cur = {}, {}, None
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip():
                continue
            k, _, v = line.strip().partition(" =")
            v = v.strip()
            if not line.startswith("\t"):
                if k == "pkgname":
                    cur = pkgs.setdefault(v, {})
                elif k == "pkgbase":
                    base["pkgbase"] = v
                continue
            target = base if cur is None else cur
            target.setdefault(k, [])
            if v:
                target[k].append(strip_ver(v))

    def get(fields, key):
        return fields.get(key, base.get(key, [])) + fields.get(key + "_i686", base.get(key + "_i686", []))

    info = {"pkgbase": base.get("pkgbase"), "pkgnames": list(pkgs), "depends": {}, "provides": {}, "arch": {},
            "makedepends": get({}, "makedepends")}
    for name, fields in pkgs.items():
        info["depends"][name] = get(fields, "depends")
        info["provides"][name] = get(fields, "provides")
        info["arch"][name] = fields.get("arch", base.get("arch", []))
    return info


def load():
    out = []
    for sub in ("arch", "tinyhat"):
        root = os.path.join(PKGBUILDS, sub)
        for base in sorted(os.listdir(root)):
            path = os.path.join(root, base, ".SRCINFO")
            if os.path.exists(path):
                info = parse(path)
                info["dir"] = f"pkgbuilds/{sub}/{base}"
                info["sub"] = sub
                info["base"] = base
                out.append(info)
    return out


def read_localdeps():
    out = {}
    with open(os.path.join(CONFIG, "localdeps.txt")) as f:
        for line in f:
            fields = line.split()
            if fields and not fields[0].startswith("#"):
                out[fields[0]] = fields[1:]
    return out


def local_index(infos, subs=("tinyhat",)):
    names, provided, base_of = {}, {}, {}
    for info in infos:
        if info["sub"] not in subs:
            continue
        for name in info["pkgnames"]:
            if not set(info["arch"][name]) & {"i686", "any"}:
                continue
            names[name] = info
            base_of[name] = info["base"]
            for pv in info["provides"].get(name, []):
                provided.setdefault(pv, name)
    return names, provided, base_of


def resolve_local(wanted, names, provided, own):
    out, queue = [], list(wanted)
    while queue:
        dep = queue.pop(0)
        name = dep if dep in names else provided.get(dep)
        if name is None or name in own or name in out:
            continue
        out.append(name)
        queue.extend(names[name]["depends"].get(name, []))
    return out


def exact_local(wanted, full, tinyhat):
    out = []
    names, provided, _ = full
    for dep in wanted:
        name = dep if dep in names else provided.get(dep)
        if name is not None and name not in out:
            out.append(name)
    for name in resolve_local([n for n in out if n in tinyhat[0]], *tinyhat[:2], set()):
        if name not in out:
            out.append(name)
    return out


def seed_index():
    try:
        return pkg.load_db()
    except SystemExit:
        return {}, {}


def auto_local(info, seed, idx, prefs):
    out = []
    for dep in all_deps(info):
        if pkg.lookup(seed, dep, prefs, set()) is not None:
            continue
        name = pkg.lookup(idx["i686"], dep, prefs, set())
        if name and name not in info["pkgnames"] and name not in out:
            out.append(name)
    return out


def all_deps(info):
    deps = list(info["makedepends"])
    for name in info["pkgnames"]:
        deps += info["depends"].get(name, [])
    return deps


def base_arches(info):
    out = set()
    for name in info["pkgnames"]:
        out.update(info["arch"][name])
    return out


def indexes(infos):
    idx = {"i686": ({}, {}), "x32": ({}, {})}
    for info in infos:
        for name in info["pkgnames"]:
            parch = info["arch"][name]
            for a in idx:
                label = "any" if "any" in parch else a if a in parch else None
                if label is None:
                    continue
                pkg.add_pkg(idx[a][0], idx[a][1], {"name": name, "arch": label, "base": info["base"],
                                                    "sub": info["sub"], "depends": info["depends"][name]},
                            info["provides"][name])
    return idx


def read_libs():
    return set(pkg.read_list(os.path.join(CONFIG, "libpkgs.txt")))


def stamp(kind, base):
    return f"$(B)/pkg/{kind}/{base}"


def x32_deps(info, idx, prefs, libs, local):
    names, provided, _ = local
    x32, tools, seen = [], [], set()
    queue = all_deps(info)
    while queue:
        dep = queue.pop(0)
        if dep in seen:
            continue
        seen.add(dep)
        name = pkg.lookup(idx["x32"], dep, prefs, set())
        p = idx["x32"][0].get(name) if name else None
        if p is None or p["arch"] != "x32":
            if p is None and (dep in names or dep in provided):
                tools.append(dep)
            continue
        if p["base"] == info["base"] or name in x32:
            continue
        if name not in libs:
            if p["sub"] == "tinyhat":
                tools.append(name)
            continue
        x32.append(name)
        queue.extend(p["depends"])
    tools = resolve_local(tools, names, provided, set(info["pkgnames"]))
    return x32, tools


def sccs(graph):
    index, low, stack, on, out, counter = {}, {}, [], set(), [], [0]
    sys.setrecursionlimit(100000)

    def visit(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on.add(v)
        for w in sorted(graph[v]):
            if w not in graph:
                continue
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = set()
            while True:
                w = stack.pop()
                on.discard(w)
                comp.add(w)
                if w == v:
                    break
            out.append(comp)

    for v in sorted(graph):
        if v not in index:
            visit(v)
    return out


def rule(lines, target, info, prereqs, args, log):
    lines.append(f"{target}: $(B)/.buildroot $(wildcard {info['dir']}/*) {' '.join(sorted(set(prereqs)))}".rstrip())
    lines.append("\t@mkdir -p $(B)/log $(@D)")
    lines.append(f"\tscripts/makepkg.sh {' '.join(args)} >$(B)/log/{log}.log 2>&1 || "
                 f"{{ tail -40 $(B)/log/{log}.log; exit 1; }}")
    lines.append("\t@touch $@")
    lines.append("")


def stamps_for(sel):
    out = set()
    for p in sel:
        out.add(stamp("x32" if p["arch"] == "x32" else "i686", p["base"]))
    return sorted(out)


def cmd_mk():
    infos = load()
    by_base = {i["base"]: i for i in infos}
    local = local_index(infos)
    names, provided, _ = local
    full = local_index(infos, ("arch", "tinyhat"))
    base_of = full[2]
    localdeps = read_localdeps()
    prefs = pkg.parse_prefs(os.path.join(CONFIG, "providers.txt"))
    libs = read_libs()
    idx = indexes(infos)
    seed = seed_index()
    x32_base = {p["name"]: p["base"] for p in idx["x32"][0].values() if p["arch"] == "x32"}
    lines, i686_stamps, x32_stamps, tinyhat = [], [], [], []

    for info in infos:
        arches = base_arches(info)
        if not arches & {"i686", "any"}:
            continue
        own = set(info["pkgnames"])
        if info["sub"] == "tinyhat":
            wanted = all_deps(info)
            tinyhat += info["pkgnames"]
            deps = resolve_local(wanted, names, provided, own)
        else:
            wanted = localdeps.get(info["base"], []) + auto_local(info, seed, idx, prefs)
            deps = [d for d in exact_local(wanted, full, local) if d not in own]
        args = [info["dir"]] + [f"i686/{d}" for d in deps]
        prereqs = [stamp("i686", base_of[d]) for d in deps]
        for d in SYSROOT.get(info["base"], []):
            args.append(f"sysroot/{d}")
            prereqs.append(stamp("x32", x32_base[d]))
        target = stamp("i686", info["base"])
        i686_stamps.append(target)
        rule(lines, target, info, prereqs, args, f"i686-{info['base']}")

    plans, graph = {}, {}
    for info in infos:
        if "x32" not in base_arches(info):
            continue
        base = info["base"]
        if base == "glibc":
            x32, tools, chain = [], [], ["x32-binutils", "x32-gcc-stage1"]
        elif base == "x32-runtime":
            x32, tools, chain = ["glibc"], [], ["x32-binutils", "x32-gcc"]
        else:
            x32, tools = x32_deps(info, idx, prefs, libs, local)
            x32 = [n for n in ("glibc", "x32-runtime") if n not in x32] + x32
            chain = ["x32-binutils", "x32-gcc"]
            if info["sub"] == "arch":
                wanted = localdeps.get(base, []) + auto_local(info, seed, idx, prefs)
                tools += [d for d in exact_local(wanted, full, local) if d not in tools and d not in info["pkgnames"]]
        plans[base] = (x32, chain + [t for t in tools if t not in chain])
        graph[base] = {x32_base[n] for n in x32} - {base}

    cycle = {}
    for comp in sccs(graph):
        if len(comp) > 1:
            for b in comp:
                cycle[b] = comp

    for base, (x32, i686) in plans.items():
        info = by_base[base]
        comp = cycle.get(base, set())
        i686_args = [f"i686/{n}" for n in i686]
        i686_pre = [stamp("i686", base_of.get(n, n)) for n in i686]
        if comp:
            outside = [n for n in x32 if x32_base[n] not in comp]
            rule(lines, stamp("boot", base), info,
                 i686_pre + [stamp("x32", x32_base[n]) for n in outside],
                 ["--arch", "x32", "--bootstrap", info["dir"]] + i686_args + [f"x32/{n}" for n in outside],
                 f"boot-{base}")
        args, prereqs = ["--arch", "x32", info["dir"]] + i686_args, list(i686_pre)
        for n in x32:
            kind = "boot" if x32_base[n] in comp else "x32"
            args.append(f"{kind}/{n}")
            prereqs.append(stamp(kind, x32_base[n]))
        target = stamp("x32", base)
        x32_stamps.append(target)
        rule(lines, target, info, prereqs, args, f"x32-{base}")

    targets, lprefs, skip = pkg.list_config(os.path.join(CONFIG, "rootfs.txt"))
    rootfs_i686 = pkg.resolve_targets(targets, idx, "i686", lprefs, skip)
    rootfs_x32 = pkg.resolve_targets(targets, idx, "x32", lprefs, skip)
    print("I686_STAMPS := " + " ".join(i686_stamps))
    print("X32_STAMPS := " + " ".join(x32_stamps))
    print("ROOTFS_I686_STAMPS := " + " ".join(stamps_for(rootfs_i686)))
    print("ROOTFS_X32_STAMPS := " + " ".join(stamps_for(rootfs_x32)))
    print("TINYHAT_PKGS := " + " ".join(sorted(tinyhat)))
    print()
    print("\n".join(lines))


def cmd_seed():
    infos = load()
    names, provided, _ = local_index(infos)
    localdeps = read_localdeps()
    db = seed_index()
    idx = indexes(infos)
    prefs = pkg.parse_prefs(os.path.join(CONFIG, "providers.txt"))
    seed = list(SEED_BASE)
    for info in infos:
        skip = set(info["pkgnames"]) | set(localdeps.get(info["base"], []))
        for dep in all_deps(info):
            if info["sub"] == "tinyhat" and (dep in names or dep in provided):
                continue
            if pkg.lookup(db, dep, prefs, set()) is None and pkg.lookup(idx["i686"], dep, prefs, set()) is not None:
                continue
            if dep in skip or dep in seed or dep.startswith("lib32-"):
                continue
            seed.append(dep)
    print("\n".join(seed))


def cmd_libs():
    os.makedirs(FILES_CACHE, exist_ok=True)
    dbnames, libnames = set(), set()
    for repo in pkg.REPOS:
        path = os.path.join(FILES_CACHE, f"{repo}.files")
        if os.environ.get("REFRESH") or not os.path.exists(path):
            pkg.download(f"{pkg.MIRROR}/{repo}/{repo}.files", path)
        with tarfile.open(path) as t:
            entries = {}
            for m in t:
                d, _, kind = m.name.partition("/")
                if kind in ("desc", "files"):
                    entries.setdefault(d, {})[kind] = t.extractfile(m).read().decode()
            for e in entries.values():
                name = re.search(r"%NAME%\n(.*)\n", e.get("desc", "")).group(1)
                dbnames.add(name)
                if re.search(r"^usr/lib/[^/]+\.(so[.0-9]*|a)$|^usr/lib/pkgconfig/[^/]+\.pc$", e.get("files", ""), re.M):
                    libnames.add(name)
    out = []
    for info in load():
        for name in info["pkgnames"]:
            if name in libnames or (info["sub"] == "tinyhat" and name not in dbnames):
                out.append(name)
    with open(os.path.join(CONFIG, "libpkgs.txt"), "w") as f:
        f.write("\n".join(sorted(set(out))) + "\n")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "mk"
    if cmd == "mk":
        cmd_mk()
    elif cmd == "seed":
        cmd_seed()
    elif cmd == "libs":
        cmd_libs()
    else:
        sys.exit("usage: mkpkgs.py mk | seed | libs")


if __name__ == "__main__":
    main()
