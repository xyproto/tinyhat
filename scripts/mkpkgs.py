#!/usr/bin/env python3
import fnmatch
import os
import re
import sys
import tarfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import distro
import pkg

TOP = pkg.TOP
SEED_BASE = ["base-devel", "pacman", "fakeroot", "git", "ccache", "python", "perl"]
SYSROOT = {"x32-gcc": ["glibc"]}
FILES_CACHE = os.path.join(TOP, "build", "cache", "files")
SEED_BROKEN = os.path.join(TOP, "build", "seed-broken.txt")
PKGCONF = distro.packages()
SEED_ONLY = {"python", "perl", "ruby", "nodejs", "lua", "tcl", "glibc", "gcc", "gcc-libs", "binutils",
             "llvm", "llvm-libs", "clang", "compiler-rt", "rust", "go"}


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

    epoch = base.get("epoch", ["0"])
    info = {"pkgbase": base.get("pkgbase"), "pkgnames": list(pkgs), "depends": {}, "provides": {}, "arch": {},
            "makedepends": get({}, "makedepends"),
            "version": f"{epoch[0] if epoch else '0'}:{base.get('pkgver', [''])[0]}"}
    for name, fields in pkgs.items():
        info["depends"][name] = get(fields, "depends")
        info["provides"][name] = get(fields, "provides")
        info["arch"][name] = fields.get("arch", base.get("arch", []))
    return info


def load():
    out, seen = [], set()
    root = os.path.join(TOP, "packages")
    for base in sorted(os.listdir(root)):
        path = os.path.join(root, base, ".SRCINFO")
        if not os.path.exists(path) or base in seen:
            continue
        info = parse(path)
        info["dir"] = f"packages/{base}"
        info["base"] = base
        info["sub"] = "tinyhat" if PKGCONF.get(base, {}).get("source") == "sources" else "arch"
        seen.add(base)
        out.append(info)
    return out


def read_localdeps():
    return distro.localdeps()


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


def rpmvercmp(a, b):
    if a == b:
        return 0
    p1 = p2 = 0
    while p1 < len(a) and p2 < len(b):
        s1, s2 = p1, p2
        while p1 < len(a) and not a[p1].isalnum():
            p1 += 1
        while p2 < len(b) and not b[p2].isalnum():
            p2 += 1
        if p1 >= len(a) or p2 >= len(b):
            break
        if p1 - s1 != p2 - s2:
            return -1 if p1 - s1 < p2 - s2 else 1
        s1, s2 = p1, p2
        isnum = a[p1].isdigit()
        test = str.isdigit if isnum else str.isalpha
        while p1 < len(a) and test(a[p1]):
            p1 += 1
        while p2 < len(b) and test(b[p2]):
            p2 += 1
        seg1, seg2 = a[s1:p1], b[s2:p2]
        if not seg2:
            return 1 if isnum else -1
        if isnum:
            seg1, seg2 = seg1.lstrip("0"), seg2.lstrip("0")
            if len(seg1) != len(seg2):
                return 1 if len(seg1) > len(seg2) else -1
        if seg1 != seg2:
            return 1 if seg1 > seg2 else -1
    if p1 >= len(a) and p2 >= len(b):
        return 0
    if (p1 >= len(a) and not b[p2].isalpha()) or (p1 < len(a) and a[p1].isalpha()):
        return -1
    return 1


def deny_rules():
    """[image] deny entries as (name pattern, operator, version): "perl*" or "llvm<23"."""
    out = []
    for e in distro.section("image").get("deny", []):
        m = re.fullmatch(r"([^<>=]+)(<=|<)(.+)", e)
        out.append((m.group(1), m.group(2), m.group(3)) if m else (e, None, None))
    return out


def denied(name, version=None, rules=None):
    """Whether a package (with this version, if known) matches [image] deny. Version rules only match known versions."""
    for pat, op, ver in deny_rules() if rules is None else rules:
        if not fnmatch.fnmatchcase(name, pat):
            continue
        if op is None:
            return True
        if version is not None:
            c = rpmvercmp(version.rpartition(":")[2].rsplit("-", 1)[0], ver)
            if c < 0 or (op == "<=" and c == 0):
                return True
    return False


def newer(local, seed):
    return rpmvercmp(local.rpartition(":")[2], seed.rpartition(":")[2].rsplit("-", 1)[0]) > 0


def seed_closure(seed, name, prefs, cache):
    if name not in cache:
        cache[name] = set()
        out, queue = set(), [name]
        while queue:
            n = queue.pop()
            if n in out:
                continue
            out.add(n)
            for d in seed[0].get(n, {}).get("depends", []):
                m = pkg.lookup(seed, d, prefs, set())
                if m is not None:
                    queue.append(m)
        cache[name] = out
    return cache[name]


def local_first(idx, name):
    p = idx["i686"][0].get(name) if name else None
    return p is not None and name not in SEED_ONLY and (p["sub"] == "tinyhat" or PKGCONF.get(p["base"], {}).get("build", False))


def auto_local(info, seed, idx, prefs, broken, cache, versions, prebuilt=frozenset(), image=frozenset()):
    hard, soft = [], []
    for dep in all_deps(info):
        seedname = pkg.lookup(seed, dep, prefs, set())
        localname = pkg.lookup(idx["i686"], dep, prefs, set())
        if localname in prebuilt and seedname is not None:
            continue
        if seedname is None or local_first(idx, localname):
            wanted, out = [localname], hard
        else:
            wanted, out = sorted(seed_closure(seed, seedname, prefs, cache) & broken), hard
            if localname in image and localname not in SEED_ONLY and localname in versions["local"] and seedname in versions["seed"] and \
                    newer(versions["local"][localname], versions["seed"][seedname]):
                soft.append(localname)
        for name in wanted:
            if name and name in idx["i686"][0] and name not in info["pkgnames"] and name not in out:
                out.append(name)
    pkgs = idx["i686"][0]
    bases = {pkgs[n]["base"] for n in hard}
    for dep in all_deps(info):
        name = pkg.lookup(idx["i686"], dep, prefs, set())
        if name in pkgs and pkgs[name]["base"] in bases and name not in info["pkgnames"] and name not in hard:
            hard.append(name)
    soft = [n for n in soft if n in pkgs and n not in info["pkgnames"] and n not in hard]
    return hard, soft


def prune_soft(plans, base_of):
    while True:
        graph = {b: {base_of[d] for d in hard + soft} - {b} for b, (_, hard, soft) in plans.items()}
        drop = 0
        for comp in sccs(graph):
            if len(comp) < 2:
                continue
            for b in comp:
                info, hard, soft = plans[b]
                keep = [d for d in soft if base_of[d] not in comp]
                drop += len(soft) - len(keep)
                plans[b] = (info, hard, keep)
        if not drop:
            return [c for c in sccs(graph) if len(c) > 1]


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
    return set(distro.lock().get("libpkgs", []))


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


MODES = os.path.join(TOP, "build", "pkg", "mode")


def mode_file(base, mode):
    os.makedirs(MODES, exist_ok=True)
    path = os.path.join(MODES, base)
    if not os.path.exists(path) or open(path).read() != mode:
        with open(path, "w") as f:
            f.write(mode)
    return f"$(B)/pkg/mode/{base}"


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
    prefs = distro.providers()
    libs = read_libs()
    idx = indexes(infos)
    seed = seed_index()
    broken = set(pkg.read_list(SEED_BROKEN))
    # Prebuilt packages older than [image] deny allows (like "llvm<23") are treated as broken, so they get built
    too_old = {n for n, p in seed[0].items() if denied(n, p["version"], [r for r in deny_rules() if r[1]])}
    broken |= too_old
    closures = {}
    versions = {"local": {n: i["version"] for i in infos for n in i["pkgnames"]},
                "seed": {p["name"]: p["version"] for p in distro.locked("buildroot")}}
    x32_base = {p["name"]: p["base"] for p in idx["x32"][0].values() if p["arch"] == "x32"}
    lines, i686_stamps, x32_stamps, tinyhat = [], [], [], []

    targets, lprefs, skip = pkg.list_config("image")
    rootfs_i686 = pkg.resolve_targets(targets, idx, "i686", lprefs, skip)
    image = {p["name"] for p in rootfs_i686}
    for n in sorted(too_old & image):
        print(f"mkpkgs.py: avoiding prebuilt {n} {seed[0][n]['version']} ([image] deny), building it", file=sys.stderr)
    listed = distro.prebuilt_listed()
    force_build = {b for b, e in PKGCONF.items() if e.get("build")}
    candidates = [b for b in listed if b not in force_build]
    if distro.load().get("prebuilt_all"):
        all_arch = [i["base"] for i in infos if i["sub"] == "arch" and i["base"] not in candidates
                    and i["base"] not in force_build]
        candidates += all_arch
    local_pkgs, local_provides = idx["i686"]

    def choose(image, metadata):
        wanted_by = {}
        for n in image:
            for d in metadata.get(n, {}).get("depends", []):
                m = pkg.lookup((metadata, local_provides), d, lprefs, set())
                if m:
                    wanted_by.setdefault(m, set()).add(n)
        roots = set(distro.section("image").get("packages", []))
        chosen, notes = set(), []
        for base in candidates:
            info = by_base.get(base)
            quiet = base not in listed
            own = [n for n in info["pkgnames"] if set(info["arch"][n]) & {"i686", "any"}] if info else []
            needed = [n for n in own if n in image and (n in roots or wanted_by.get(n, set()) - set(own))] or own
            if quiet:
                available = needed and all(pkg.lookup(seed, n, prefs, set()) is not None for n in needed)
            else:
                available = set(own) & set(seed[0])
            deps = {pkg.lookup(seed, d, prefs, set()) for n in needed if n in seed[0] for d in seed[0][n]["depends"]}
            if not available:
                notes.append(f"{base} is not available prebuilt, building it")
            elif set(info["pkgnames"]) & broken:
                notes.append(f"prebuilt {base} is broken in the seed, building it")
            elif deps & broken:
                notes.append(f"prebuilt {base} needs {' '.join(sorted(deps & broken))}, which is broken in the seed, building it")
            else:
                chosen.add(base)
        return chosen, [n for n in notes if n.split()[0] in listed or n.split()[1] in listed]

    metadata = local_pkgs
    for _ in range(10):
        prebuilt, notes = choose(image, metadata)
        prebuilt_names = {n for b in prebuilt for n in by_base[b]["pkgnames"]}
        as_built = {n: dict(p, depends=seed[0][n]["depends"]) if n in prebuilt_names and n in seed[0] else p
                    for n, p in local_pkgs.items()}
        rootfs_i686 = pkg.resolve_targets(targets, {"i686": (as_built, local_provides)}, "i686", lprefs, skip)
        new_image = {p["name"] for p in rootfs_i686}
        if new_image == image and metadata is as_built:
            break
        image, metadata = new_image, as_built
    for note in notes:
        print(f"mkpkgs.py: {note}", file=sys.stderr)

    i686_plans = {}
    for info in infos:
        arches = base_arches(info)
        if not arches & {"i686", "any"}:
            continue
        own = set(info["pkgnames"])
        soft = []
        if info["base"] in prebuilt:
            target = stamp("i686", info["base"])
            i686_stamps.append(target)
            names_here = [n for n in info["pkgnames"] if set(info["arch"][n]) & {"i686", "any"}]
            names_here = [n for n in names_here if n in image] or names_here
            lines.append(f"{target}: $(B)/.buildroot {mode_file(info['base'], 'prebuilt')}")
            lines.append("\t@mkdir -p $(@D)")
            lines.append(f"\tpython3 scripts/pkg.py prebuilt $(REPO) vendor/archlinux32/pkg.lock {' '.join(names_here)}")
            lines.append("\t@touch $@")
            lines.append("")
            continue
        if info["sub"] == "tinyhat":
            wanted = all_deps(info)
            tinyhat += info["pkgnames"]
            deps = [d for d in exact_local(localdeps.get(info["base"], []) + resolve_local(wanted, names, provided, own), full, local) if d not in own]
        else:
            hard, soft = auto_local(info, seed, idx, prefs, broken, closures, versions, prebuilt_names, image)
            deps = [d for d in exact_local(localdeps.get(info["base"], []) + hard, full, local) if d not in own]
            soft = [d for d in exact_local(soft, full, local) if d not in own and d not in deps]
        i686_plans[info["base"]] = (info, deps, soft)
    for comp in prune_soft(i686_plans, base_of):
        print(f"mkpkgs.py: dependency cycle: {' '.join(sorted(comp))}", file=sys.stderr)

    for info, deps, soft in i686_plans.values():
        deps = deps + soft
        args = [info["dir"]] + [f"i686/{d}" for d in deps]
        prereqs = [stamp("i686", base_of[d]) for d in deps]
        for d in SYSROOT.get(info["base"], []):
            args.append(f"sysroot/{d}")
            prereqs.append(stamp("x32", x32_base[d]))
        target = stamp("i686", info["base"])
        i686_stamps.append(target)
        prereqs.append(mode_file(info["base"], "built"))
        rule(lines, target, info, prereqs, args, f"i686-{info['base']}")

    x32_only = set(distro.load()["x32"]) if "x32" in distro.load() else None
    plans, graph = {}, {}
    for info in infos:
        if "x32" not in base_arches(info):
            continue
        base = info["base"]
        if x32_only is not None and base not in x32_only:
            continue
        if base == "glibc":
            x32, tools, chain = [], [], ["x32-binutils", "x32-gcc-stage1"]
        elif base == "x32-runtime":
            x32, tools, chain = ["glibc"], [], ["x32-binutils", "x32-gcc"]
        else:
            x32, tools = x32_deps(info, idx, prefs, libs, local)
            x32 = [n for n in ("glibc", "x32-runtime") if n not in x32] + x32
            chain = ["x32-binutils", "x32-gcc"]
            if info["sub"] == "arch":
                wanted = localdeps.get(base, []) + auto_local(info, seed, idx, prefs, broken, closures, versions)[0]
                tools += [d for d in exact_local(wanted, full, local) if d not in tools and d not in info["pkgnames"]]
        if x32_only is not None:
            x32 = [n for n in x32 if x32_base.get(n) in x32_only]
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

    x32_index = idx["x32"]
    if x32_only is not None:
        kept, kept_provides = {}, {}
        for n, p in idx["x32"][0].items():
            if p["arch"] == "any" or p["base"] in x32_only:
                pkg.add_pkg(kept, kept_provides, p, [pv for pv, names in idx["x32"][1].items() if n in names])
        x32_index = (kept, kept_provides)
    rootfs_x32 = pkg.resolve_targets(targets, {"i686": (as_built, local_provides), "x32": x32_index}, "x32", lprefs, skip)
    print("I686_STAMPS := " + " ".join(i686_stamps))
    print("X32_STAMPS := " + " ".join(x32_stamps))
    print("ROOTFS_I686_STAMPS := " + " ".join(stamps_for(rootfs_i686)))
    print("ROOTFS_X32_STAMPS := " + " ".join(stamps_for(rootfs_x32)))
    print("TINYHAT_PKGS := " + " ".join(sorted(tinyhat)))
    print()
    print("\n".join(lines))


def image_bases():
    path = os.path.join(TOP, "build", "pkgs.mk")
    if not os.path.exists(path):
        return None
    deps, cur = {}, None
    text = open(path).read()
    for line in text.splitlines():
        m = re.match(r"^\$\(B\)/pkg/(?:i686|x32)/([^:\s]+):(.*)$", line)
        if m:
            cur = m.group(1)
            deps.setdefault(cur, set()).update(re.findall(r"\$\(B\)/pkg/(?:i686|x32)/([^\s)]+)", m.group(2)))
        elif cur and line.startswith("\t"):
            deps[cur].update(re.findall(r"(?:i686|x32|sysroot)/([A-Za-z0-9@._+-]+)", line))
        else:
            cur = None
    roots = set()
    for v in re.findall(r"^ROOTFS_\w*STAMPS := (.*)$", text, re.M):
        roots |= {x.rsplit("/", 1)[1] for x in v.split()}
    with open(os.path.join(TOP, "Makefile")) as f:
        roots |= set(re.findall(r"pkg/(?:i686|x32)/([a-z0-9][A-Za-z0-9@._+-]*)", f.read()))
    need, todo = set(), list(roots)
    while todo:
        b = todo.pop()
        if b not in need:
            need.add(b)
            todo += deps.get(b, ())
    return need


def cmd_seed():
    infos = load()
    used = image_bases()
    names, provided, _ = local_index(infos)
    localdeps = read_localdeps()
    db = seed_index()
    idx = indexes(infos)
    prefs = distro.providers()
    seed = list(SEED_BASE)
    for info in infos:
        if used is not None and info["base"] not in used:
            continue
        skip = set(info["pkgnames"]) | set(localdeps.get(info["base"], []))
        for dep in all_deps(info):
            if info["sub"] == "tinyhat" and (dep in names or dep in provided):
                continue
            if local_first(idx, pkg.lookup(idx["i686"], dep, prefs, set())):
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
    distro.write_lock(libpkgs=sorted(set(out)))


def cmd_lock_arch(arch):
    """Lock the prebuilt packages of the image for another architecture ([image-ARCH] in distro.toml).

    The image is [image] with the packages in remove left out and the ones in add added. Packages come
    prebuilt (pkg.ARCH_SOURCES), except for the recipes that are built from source:
    - Tiny Hat's own recipes that the prebuilt repositories lack, that change something (patches or
      other files next to the PKGBUILD) or that are listed in build,
    - recipes with a version pinned in distro.toml, when the prebuilt version is different,
    - recipes for prebuilt packages that would pull in something [image] deny names that the x86 image
      does not have (like X11 or SDL2), repeated until nothing new is pulled in.
    The versions and checksums of the prebuilt packages are written to [image-ARCH] in distro.lock."""
    section = f"image-{arch}"
    conf, base = distro.section(section), distro.section("image")
    remove = set(conf.get("remove", []))
    targets = [t for t in base.get("packages", []) if t not in remove]
    targets += [t for t in conf.get("add", []) if t not in targets]
    skip = set(base.get("skip", [])) | set(conf.get("skip", []))
    prefs = distro.providers()
    db, provides = pkg.arch_db(arch)
    infos = load()
    by_base = {i["base"]: i for i in infos}
    base_of = {n: i["base"] for i in infos for n in i["pkgnames"]}
    rules = deny_rules()
    name_rules = [r for r in rules if not r[1]]
    x86 = {p["name"] for p in pkg.resolve_targets(base.get("packages", []), indexes(infos), "i686", prefs, skip)}

    local = set(conf.get("build", []))
    for info in infos:
        changed = set(os.listdir(os.path.join(TOP, info["dir"]))) - {"PKGBUILD", ".SRCINFO", "LICENSE"}
        if info["sub"] == "tinyhat" and (changed or any(n not in db for n in info["pkgnames"])):
            local.add(info["base"])
        # A version pinned in distro.toml is pinned for a reason, so build it if the prebuilt one differs
        pin = distro.tables().get(info["base"], {}).get("version")
        if pin and any(n in db and db[n]["version"] != pin for n in info["pkgnames"]):
            local.add(info["base"])

    def resolve():
        pkgs, provs = {}, {}
        for b in sorted(local):
            for name in by_base[b]["pkgnames"]:
                pkg.add_pkg(pkgs, provs, {"name": name, "base": b, "local": True,
                                          "depends": by_base[b]["depends"][name]}, by_base[b]["provides"][name])
        for name, p in db.items():
            pkg.add_pkg(pkgs, provs, p, [])
        for pv, names in provides.items():
            provs.setdefault(pv, []).extend(n for n in names if n not in provs.get(pv, []))
        sel = pkg.resolve_targets(targets, {arch: (pkgs, provs)}, arch, prefs, skip)
        names = {p["name"] for p in sel}
        users = {}
        for p in sel:
            for d in p["depends"]:
                users.setdefault(pkg.lookup((pkgs, provs), d, prefs, names), []).append(p)
        bad = {p["name"] for p in sel if p["name"] not in x86 and denied(p["name"], None, name_rules)}
        return sel, users, bad

    while True:
        sel, users, bad = resolve()
        more = {base_of[u["name"]] for n in bad for u in users.get(n, [])
                if not u.get("local") and u["name"] not in bad and u["name"] in base_of} - local
        if not more:
            break
        local |= more

    built = sorted({p["base"] for p in sel if p.get("local")})
    prebuilt = {p["name"]: {"repo": p["repo"], "version": p["version"], "file": p["filename"], "sha256": p["sha256"]}
                for p in sel if not p.get("local")}
    distro.write_lock(**{section: prebuilt})
    print(f"mkpkgs.py: {section}: {len(prebuilt)} prebuilt packages locked, {len(built)} recipes to build: "
          + " ".join(built), file=sys.stderr)
    for n in sorted(bad):
        print(f"mkpkgs.py: {section}: {n} ([image] deny) is still pulled in by "
              + " ".join(sorted(u["name"] for u in users.get(n, []))), file=sys.stderr)
    old = sorted(f"{n} {e['version']}" for n, e in prebuilt.items() if denied(n, e["version"], [r for r in rules if r[1]]))
    if old:
        sys.exit(f"mkpkgs.py: {section}: older than [image] deny allows: {', '.join(old)}")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "mk"
    if cmd == "mk":
        cmd_mk()
    elif cmd == "seed":
        cmd_seed()
    elif cmd == "libs":
        cmd_libs()
    elif cmd == "lock-arch" and len(sys.argv) == 3:
        cmd_lock_arch(sys.argv[2])
    else:
        sys.exit("usage: mkpkgs.py mk | seed | libs | lock-arch ARCH")


if __name__ == "__main__":
    main()
