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


def parse(path):
    base, pkgs, cur, constraints = {}, {}, None, {}
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
                target[k].append(pkg.strip_ver(v))
                m = re.fullmatch(r"([^<>=]+)(>=|<=|=|>|<)(.+)", v)
                if m and k.partition("_")[0] in ("depends", "makedepends"):
                    constraints.setdefault(m.group(1), []).append((m.group(2), m.group(3)))

    def get(fields, key):
        return fields.get(key, base.get(key, [])) + fields.get(key + "_i686", base.get(key + "_i686", []))

    epoch = base.get("epoch", ["0"])
    info = {"pkgbase": base.get("pkgbase"), "pkgnames": list(pkgs), "depends": {}, "provides": {}, "arch": {},
            "makedepends": get({}, "makedepends"), "constraints": constraints,
            "version": f"{epoch[0] if epoch else '0'}:{base.get('pkgver', [''])[0]}",
            "release": base.get("pkgrel", ["1"])[0]}
    for name, fields in pkgs.items():
        info["depends"][name] = get(fields, "depends")
        info["provides"][name] = get(fields, "provides")
        info["arch"][name] = fields.get("arch", base.get("arch", []))
    return info


def load():
    out = []
    root = os.path.join(TOP, "packages")
    skipped = distro.skipped() | distro.excluded()
    for base in sorted(os.listdir(root)):
        path = os.path.join(root, base, ".SRCINFO")
        if base in skipped or not os.path.exists(path):
            continue
        info = parse(path)
        info["dir"] = f"packages/{base}"
        info["base"] = base
        info["sub"] = "tinyhat" if PKGCONF.get(base, {}).get("source") == "sources" else "arch"
        out.append(info)
    return out


def package_index(infos, subs=("tinyhat",), arches=("i686", "any")):
    """The packages that the TINYBUILDs in subs make for arches, each mapped to its package directory."""
    idx = pkg.Index.new()
    for info in infos:
        if info["sub"] not in subs:
            continue
        for name in info["pkgnames"]:
            if arches and not set(info["arch"][name]) & set(arches):
                continue
            idx.add(name, info, info["provides"].get(name, []))
    return idx


def runtime_deps(idx):
    return lambda name: idx.pkgs[name]["depends"].get(name, [])


def exact_local(wanted, full, tinyhat):
    out = []
    for dep in wanted:
        name = full.resolve(dep)
        if name is not None and name not in out:
            out.append(name)
    for name in tinyhat.closure([n for n in out if n in tinyhat.pkgs], runtime_deps(tinyhat)):
        if name not in out:
            out.append(name)
    return out


def seed_index():
    try:
        return pkg.load_db()
    except SystemExit:
        return pkg.Index.new()


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


def vercmp(a, b):
    """Compare two pacman versions [epoch:]version[-release]; the release only counts when both have one."""
    def split(v):
        epoch, _, rest = v.rpartition(":")
        version, _, release = rest.partition("-")
        return int(epoch or 0), version, release
    (e1, v1, r1), (e2, v2, r2) = split(a), split(b)
    if e1 != e2:
        return -1 if e1 < e2 else 1
    return rpmvercmp(v1, v2) or (rpmvercmp(r1, r2) if r1 and r2 else 0)


def satisfies(version, constraints):
    ops = {">=": lambda c: c >= 0, "<=": lambda c: c <= 0, "=": lambda c: c == 0, ">": lambda c: c > 0, "<": lambda c: c < 0}
    return all(ops[op](vercmp(version, req)) for op, req in constraints)


def deny_rules():
    """[deny] image entries as (name pattern, operator, version): "perl*" or "llvm<23"."""
    out = []
    for e in distro.deny_image():
        m = re.fullmatch(r"([^<>=]+)(<=|<)(.+)", e)
        out.append((m.group(1), m.group(2), m.group(3)) if m else (e, None, None))
    return out + [(name, None, None) for name in sorted(distro.excluded())]


def denied(name, version=None, rules=None):
    """Whether a package (with this version, if known) matches [deny] image. Version rules only match known versions."""
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
                m = seed.resolve(d, prefs)
                if m is not None:
                    queue.append(m)
        cache[name] = out
    return cache[name]


def local_first(idx, name):
    p = idx["i686"][0].get(name) if name else None
    return p is not None and name not in SEED_ONLY and (p["sub"] == "tinyhat" or PKGCONF.get(p["base"], {}).get("build_from_source", False))


def auto_local(info, seed, idx, prefs, broken, cache, versions, prebuilt=frozenset(), image=frozenset()):
    hard, soft = [], []
    for dep in all_deps(info):
        seedname = seed.resolve(dep, prefs)
        localname = idx["i686"].resolve(dep, prefs)
        wants = info["constraints"].get(dep)
        too_old = bool(wants and seedname in seed[0] and localname is not None
                       and not satisfies(seed[0][seedname]["version"], wants))
        if localname in prebuilt and seedname is not None and not too_old:
            continue
        if seedname is None or local_first(idx, localname) or too_old:
            wanted, out = [localname], hard
        else:
            wanted, out = [idx["i686"].resolve(n, prefs) or n
                           for n in sorted(seed_closure(seed, seedname, prefs, cache) & broken)], hard
            if localname in image and localname not in SEED_ONLY and localname in versions["local"] and seedname in versions["seed"] and \
                    newer(versions["local"][localname], versions["seed"][seedname]):
                soft.append(localname)
        for name in wanted:
            if name and name in idx["i686"][0] and name not in info["pkgnames"] and name not in out:
                out.append(name)
    pkgs = idx["i686"][0]
    bases = {pkgs[n]["base"] for n in hard}
    for dep in all_deps(info):
        name = idx["i686"].resolve(dep, prefs)
        if name in pkgs and pkgs[name]["base"] in bases and name not in info["pkgnames"] and name not in hard:
            hard.append(name)
    soft = [n for n in soft if n in pkgs and n not in info["pkgnames"] and n not in hard]
    return hard, soft


def prune_runtime(plans, base_of, idx, prefs):
    graph = {b: {base_of[d] for d in hard} - {b} for b, (_, hard, _) in plans.items()}
    for comp in sccs(graph):
        if len(comp) < 2:
            continue
        for b in comp:
            info, hard, soft = plans[b]
            build = {idx["i686"].resolve(d, prefs) for d in info["makedepends"]}
            plans[b] = (info, [d for d in hard if base_of[d] not in comp or d in build], soft)


def reachable(graph, b):
    seen, todo = set(), [b]
    while todo:
        for n in graph.get(todo.pop(), ()):
            if n not in seen:
                seen.add(n)
                todo.append(n)
    return seen


def break_seeded(deps, base, seeded):
    deps = dict(deps)
    while True:
        graph = {b: {base(d) for d in ds} - {b} for b, ds in deps.items()}
        changed = False
        for comp in sccs(graph):
            if len(comp) < 2:
                continue
            for b in sorted(comp, key=lambda b: (len(reachable(graph, b)), b), reverse=True):
                keep = [d for d in deps[b] if base(d) not in comp or d not in seeded]
                if len(keep) < len(deps[b]):
                    deps[b] = keep
                    changed = True
                    break
        if not changed:
            return deps


def prune_seeded(plans, base_of, seeded):
    hard = break_seeded({b: h for b, (_, h, _) in plans.items()}, lambda d: base_of[d], seeded)
    for b, (info, _, soft) in list(plans.items()):
        plans[b] = (info, hard[b], soft)


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
    idx = {"i686": pkg.Index.new(), "x32": pkg.Index.new()}
    for info in infos:
        for name in info["pkgnames"]:
            parch = info["arch"][name]
            for a in idx:
                label = "any" if "any" in parch else a if a in parch else None
                if label is None:
                    continue
                idx[a].add(name, {"name": name, "arch": label, "base": info["base"], "sub": info["sub"],
                                  "depends": info["depends"][name]}, info["provides"][name])
    return idx


def read_libs():
    return set(distro.lock().get("libpkgs", []))


def stamp(kind, base):
    return f"$(B)/pkg/{kind}/{base}"


def x32_deps(info, idx, prefs, libs, local):
    x32, tools, seen = [], [], set()
    queue = all_deps(info)
    while queue:
        dep = queue.pop(0)
        if dep in seen:
            continue
        seen.add(dep)
        name = idx["x32"].resolve(dep, prefs)
        p = idx["x32"][0].get(name) if name else None
        if p is None or p["arch"] != "x32":
            if p is None and local.resolve(dep) is not None:
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
    tools = local.closure(tools, runtime_deps(local), set(info["pkgnames"]))
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
    distro.write_if_changed(os.path.join(MODES, base), mode)
    return f"$(B)/pkg/mode/{base}"


VERS = os.path.join(TOP, "build", "pkg", "ver")
VERSION_OF = {}
NAME_BASE = {}


VER_WRITTEN = {}


def ver_file(kind, name, version):
    d = os.path.join(VERS, kind)
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, name)
    if VER_WRITTEN.setdefault(path, version) != version:
        sys.exit(f"buildplan.py: {kind}/{name} is both {VER_WRITTEN[path]} and {version}; its version stamp would change on every run")
    fresh = not os.path.exists(path)
    distro.write_if_changed(path, f"{version}\n")
    if fresh:
        os.utime(path, (0, 0))
    return f"$(B)/pkg/ver/{kind}/{name}"


def declared(info, lockname):
    kind = lockname.partition("-")[2] or "i686"
    seeded = {p["name"]: p["version"] for p in distro.locked(lockname)}
    out = []
    for d in all_deps(info) + list(info.get("checkdepends", [])):
        name = re.split(r"[<>=]", d, maxsplit=1)[0]
        if name in info["pkgnames"]:
            continue
        base = NAME_BASE.get(kind, {}).get(name)
        if base is not None and (kind, base) in VERSION_OF:
            out.append(ver_file(f"base-{kind}", base, VERSION_OF[(kind, base)]))
        elif name in seeded:
            out.append(ver_file(kind, name, seeded[name]))
    return out


def dep_prereqs(prereqs):
    vers, order = [], []
    for p in prereqs:
        m = re.match(r"^\$\(B\)/pkg/(i686|x32|boot|aarch64|x86_64)/(.+)$", p)
        if m and (m.group(1), m.group(2)) in VERSION_OF:
            vers.append(ver_file(f"base-{m.group(1)}", m.group(2), VERSION_OF[(m.group(1), m.group(2))]))
            order.append(p)
        else:
            vers.append(p)
    return vers, order


def rule(lines, target, info, prereqs, args, log, buildroot="$(B)/.buildroot", lockname="buildroot"):
    vers, order = dep_prereqs(prereqs)
    vers += declared(info, lockname)
    if distro.tables().get(info["base"], {}).get("theme"):
        vers.append("$(B)/theme.toml")
    nested = any(os.path.isdir(os.path.join(TOP, info["dir"], e)) for e in os.listdir(os.path.join(TOP, info["dir"])))
    inputs = f"$(shell find {info['dir']} -type f ! -name .SRCINFO)" if nested else f"$(wildcard {info['dir']}/*)"
    lines.append(f"{target}: {inputs} {' '.join(sorted(set(vers)))} | {buildroot} {' '.join(sorted(set(order)))}".rstrip())
    lines.append("\t@mkdir -p $(B)/log $(@D)")
    lines.append(f"\tscripts/buildpackage.sh {' '.join(args)} >$(B)/log/{log}.log 2>&1 || "
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
    local = package_index(infos)
    full = package_index(infos, ("arch", "tinyhat"))
    base_of = {n: info["base"] for n, info in full.pkgs.items()}
    prefs = distro.providers()
    libs = read_libs()
    idx = indexes(infos)
    seed = seed_index()
    broken = set(pkg.read_list(SEED_BROKEN))
    # Prebuilt packages older than [image] deny allows (like "llvm<23") are treated as broken, so they get built
    too_old = {n for n, p in seed[0].items() if denied(n, p["version"], [r for r in deny_rules() if r[1]])}
    broken |= too_old | {n for n, p in seed[0].items() if pkg.python_bound(p)}
    broken |= set(distro.section("buildroot").get("i686_skip", [])) & set(seed[0])
    closures = {}
    versions = {"local": {n: i["version"] for i in infos for n in i["pkgnames"]},
                "seed": {p["name"]: p["version"] for p in distro.locked("buildroot")}}
    x32_base = {p["name"]: p["base"] for p in idx["x32"][0].values() if p["arch"] == "x32"}
    lines, i686_stamps, x32_stamps, tinyhat = [], [], [], []

    targets, lprefs, skip = pkg.list_config("image")
    rootfs_i686 = pkg.resolve_targets(targets, idx, "i686", lprefs, skip)
    image = {p["name"] for p in rootfs_i686}
    for n in sorted(too_old & image):
        print(f"buildplan.py: avoiding prebuilt {n} {seed[0][n]['version']} ([image] deny), building it", file=sys.stderr)
    listed = distro.prebuilt_listed()
    force_build = {b for b, e in PKGCONF.items() if e.get("build_from_source")}
    candidates = [b for b in listed if b not in force_build]
    if distro.load().get("prebuilt_all"):
        all_arch = [i["base"] for i in infos if i["sub"] == "arch" and i["base"] not in candidates
                    and i["base"] not in force_build]
        candidates += all_arch
    local_pkgs, local_provides = idx["i686"]
    with_metadata = lambda metadata: pkg.Index(metadata, local_provides)

    def choose(image, metadata):
        wanted_by = {}
        for n in image:
            for d in metadata.get(n, {}).get("depends", []):
                m = with_metadata(metadata).resolve(d, lprefs)
                if m:
                    wanted_by.setdefault(m, set()).add(n)
        roots = set(distro.section("image").get("packages", []))
        chosen, notes = set(), []
        for base in candidates:
            info = by_base.get(base)
            own = [n for n in info["pkgnames"] if set(info["arch"][n]) & {"i686", "any"}] if info else []
            needed = [n for n in own if n in image and (n in roots or wanted_by.get(n, set()) - set(own))] or own
            if base in listed:
                available = set(own) & set(seed[0])
            else:
                available = needed and all(seed.resolve(n, prefs) is not None for n in needed)
            deps = {seed.resolve(d, prefs) for n in needed if n in seed[0] for d in seed[0][n]["depends"]}
            if not available:
                note = f"{base} is not available prebuilt, building it"
            elif (set(info["pkgnames"]) | {seed.resolve(n, prefs) for n in needed}) & broken:
                note = f"prebuilt {base} is broken in the seed, building it"
            elif deps & broken:
                note = f"prebuilt {base} needs {' '.join(sorted(deps & broken))}, which is broken in the seed, building it"
            else:
                chosen.add(base)
                continue
            if base in listed:
                notes.append(note)
        return chosen, notes

    metadata = local_pkgs
    for _ in range(10):
        prebuilt, notes = choose(image, metadata)
        prebuilt_names = {n for b in prebuilt for n in by_base[b]["pkgnames"]}
        as_built = {n: dict(p, depends=seed[0][n]["depends"]) if n in prebuilt_names and n in seed[0] else p
                    for n, p in local_pkgs.items()}
        rootfs_i686 = pkg.resolve_targets(targets, {"i686": with_metadata(as_built)}, "i686", lprefs, skip)
        new_image = {p["name"] for p in rootfs_i686}
        if new_image == image and metadata is as_built:
            break
        image, metadata = new_image, as_built
    for note in notes:
        print(f"buildplan.py: {note}", file=sys.stderr)

    for info in infos:
        for kind in ("i686", "x32", "boot"):
            VERSION_OF[(kind, info["base"])] = f"{info['version']}-{info['release']}"
            for n in info["pkgnames"]:
                NAME_BASE.setdefault(kind, {})[n] = info["base"]
        for kind in ("i686", "x32", "boot"):
            for n in info["pkgnames"]:
                for pv in info["provides"][n]:
                    NAME_BASE[kind].setdefault(pv, info["base"])
    for b in prebuilt:
        VERSION_OF[("i686", b)] = "prebuilt " + " ".join(
            f"{n}={seed[0][n]['version']}" for n in sorted(by_base[b]["pkgnames"]) if n in seed[0])
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
            seeded = [ver_file("seed-buildroot", n, seed[0][n]["version"]) for n in names_here if n in seed[0]]
            lines.append(f"{target}: {mode_file(info['base'], 'prebuilt')} {' '.join(seeded)} | $(B)/.buildroot".replace("  ", " "))
            lines.append("\t@mkdir -p $(@D)")
            lines.append(f"\tpython3 scripts/pkg.py prebuilt $(REPO) vendor/archlinux32/pkg.lock {' '.join(names_here)}")
            lines.append("\t@touch $@")
            lines.append("")
            continue
        if info["sub"] == "tinyhat":
            tinyhat += info["pkgnames"]
        hard, soft = auto_local(info, seed, idx, prefs, broken, closures, versions, prebuilt_names, image)
        deps = [d for d in exact_local(hard, full, local) if d not in own]
        soft = [d for d in exact_local(soft, full, local) if d not in own and d not in deps]
        i686_plans[info["base"]] = (info, deps, soft)
    prune_runtime(i686_plans, base_of, idx, prefs)
    prune_seeded(i686_plans, base_of, {p["name"] for p in distro.locked("buildroot")})
    for comp in prune_soft(i686_plans, base_of):
        print(f"buildplan.py: dependency cycle: {' '.join(sorted(comp))}", file=sys.stderr)

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
                wanted = auto_local(info, seed, idx, prefs, broken, closures, versions, prebuilt_names)[0]
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
        x32_index = pkg.Index.new()
        for n, p in idx["x32"].pkgs.items():
            if p["arch"] == "any" or p["base"] in x32_only:
                x32_index.add(n, p, idx["x32"].provided_by(n))
    x32_targets, _, x32_skip = pkg.list_config("image-x32") if "image-x32" in distro.load() else (targets, lprefs, skip)
    rootfs_x32 = pkg.resolve_targets(x32_targets, {"i686": with_metadata(as_built), "x32": x32_index}, "x32", lprefs, x32_skip)
    aarch64_stamps = arch_rules(lines, "aarch64", by_base, prefs)
    x86_64_stamps = arch_rules(lines, "x86_64", by_base, prefs)
    print("I686_STAMPS := " + " ".join(i686_stamps))
    print("AARCH64_STAMPS := " + " ".join(aarch64_stamps))
    print("X86_64_STAMPS := " + " ".join(x86_64_stamps))
    print("X32_STAMPS := " + " ".join(x32_stamps))
    print("ROOTFS_I686_STAMPS := " + " ".join(stamps_for(rootfs_i686)))
    print("ROOTFS_X32_STAMPS := " + " ".join(stamps_for(rootfs_x32)))
    print("TINYHAT_PKGS := " + " ".join(sorted(tinyhat)))
    roots = "\n".join([f"ROOTFS_I686_STAMPS := {' '.join(stamps_for(rootfs_i686))}",
                       f"ROOTFS_X32_STAMPS := {' '.join(stamps_for(rootfs_x32))}"] + lines)
    need = needed_package_dirs(infos, roots)
    print("PACKAGES_STAMPS := " + " ".join(s for s in i686_stamps if s.rsplit("/", 1)[1] in need))
    print()
    print("\n".join(lines))


def arch_rules(lines, arch, by_base, prefs):
    """Rules for the package directories that "make lock-ARCH" decided to build for another architecture ([build-ARCH] in
    distro.lock). Each one gets the other packages built for that architecture that it needs."""
    built = [by_base[b] for b in sorted(distro.lock().get(f"build-{arch}", {})) if b in by_base]
    here = package_index(built, ("arch", "tinyhat"), arches=())
    names = here.pkgs
    plans = {}
    for info in built:
        own = set(info["pkgnames"])
        plans[info["base"]] = (here.closure(all_deps(info), runtime_deps(here), own),
                               here.closure(info["makedepends"], runtime_deps(here), own))
    graph = {b: {names[d]["base"] for d in deps} - {b} for b, (deps, _) in plans.items()}
    for comp in sccs(graph):
        for b in comp if len(comp) > 1 else ():
            deps, make = plans[b]
            plans[b] = ([d for d in deps if names[d]["base"] not in comp or d in make], make)
    seeded = set(distro.lock().get(f"buildroot-{arch}", {}))
    hard = break_seeded({b: deps for b, (deps, _) in plans.items()}, lambda d: names[d]["base"], seeded)
    plans = {b: (hard[b], make) for b, (_, make) in plans.items()}
    stamps = []
    for info in built:
        VERSION_OF[(arch, info["base"])] = f"{info['version']}-{info['release']}"
        for n in info["pkgnames"]:
            NAME_BASE.setdefault(arch, {})[n] = info["base"]
    for info in built:
        for n in info["pkgnames"]:
            for pv in info["provides"][n]:
                NAME_BASE[arch].setdefault(pv, info["base"])
    for info in built:
        deps = plans[info["base"]][0]
        target = stamp(arch, info["base"])
        stamps.append(target)
        rule(lines, target, info, [stamp(arch, names[d]["base"]) for d in deps],
             ["--arch", arch, info["dir"]] + [f"{arch}/{d}" for d in deps], f"{arch}-{info['base']}",
             f"$(B)/.buildroot-{arch}", f"buildroot-{arch}")
    return stamps


def image_bases():
    path = os.path.join(TOP, "build", "pkgs.mk")
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return needed_bases(f.read())


def prebuilt_bases():
    """The package directories that build/pkgs.mk takes prebuilt, from the seed, instead of building them."""
    path = os.path.join(TOP, "build", "pkgs.mk")
    if not os.path.exists(path):
        return set()
    out, cur = set(), None
    with open(path) as f:
        for line in f:
            m = re.match(r"^\$\(B\)/pkg/i686/([^:\s]+):", line)
            if m:
                cur = m.group(1)
            elif cur and line.startswith("\tpython3 scripts/pkg.py prebuilt "):
                out.add(cur)
            elif not line.startswith("\t"):
                cur = None
    return out


def needed_bases(text):
    """The package directories (i686 and x32) that the images and the Makefile need, with what it takes to build them,
    from the text of build/pkgs.mk."""
    deps, cur = {}, None
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


def needed_package_dirs(infos, text):
    """The package directories needed for the images: the ones the images and the Makefile use (from the text of
    build/pkgs.mk) and the aarch64 and x86_64 builds, and everything their TINYBUILDs depend on, at build time or at run
    time, whether or not that ends up built here."""
    full = package_index(infos, ("arch", "tinyhat"))
    by_base = {i["base"]: i for i in infos}
    need = set(needed_bases(text)) | set(distro.lock().get("build-aarch64", {})) | set(distro.lock().get("build-x86_64", {}))
    todo = [b for b in need if b in by_base]
    while todo:
        info = by_base[todo.pop()]
        for dep in all_deps(info):
            name = full.resolve(dep)
            base = full.pkgs[name]["base"] if name else None
            if base is not None and base not in need:
                need.add(base)
                todo.append(base)
    return need


def cmd_seed():
    infos = load()
    used = image_bases()
    local = package_index(infos)
    db = seed_index()
    idx = indexes(infos)
    prefs = distro.providers()
    seed = list(SEED_BASE)
    excluded, wanted_by = distro.excluded(), {}
    # A package that is taken prebuilt is never built, so what it takes to build it is not needed
    prebuilt = prebuilt_bases()
    for info in infos:
        if used is not None and info["base"] not in used or info["base"] in prebuilt:
            continue
        skip = set(info["pkgnames"])
        for dep in all_deps(info):
            if info["sub"] == "tinyhat" and local.resolve(dep) is not None:
                continue
            localname = idx["i686"].resolve(dep, prefs)
            if local_first(idx, localname):
                continue
            if localname is not None and db.resolve(dep, prefs) is None:
                continue
            if dep in skip or dep in seed or dep.startswith("lib32-"):
                continue
            if dep in excluded:
                wanted_by.setdefault(dep, []).append(info["base"])
                continue
            seed.append(dep)
    if wanted_by:
        sys.exit("buildplan.py: TINYBUILDs need excluded packages in the build environment:\n" +
                 "\n".join(f"  {d}: needed by {', '.join(sorted(b))}" for d, b in sorted(wanted_by.items())))
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
                    entries.setdefault(d, {}).update(pkg.parse_desc(t.extractfile(m).read().decode()))
            for e in entries.values():
                if "NAME" not in e:
                    continue
                name = e["NAME"][0]
                dbnames.add(name)
                if any(re.fullmatch(r"usr/lib/[^/]+\.(so[.0-9]*|a)|usr/lib/pkgconfig/[^/]+\.pc", f) for f in e.get("FILES", [])):
                    libnames.add(name)
    out = []
    for info in load():
        for name in info["pkgnames"]:
            if name in libnames or (info["sub"] == "tinyhat" and name not in dbnames):
                out.append(name)
    distro.write_lock(libpkgs=sorted(set(out)))


def cmd_lock_arch(arch, check=False):
    """Lock the prebuilt packages of the image for another architecture ([image-ARCH] in distro.toml).

    The image is [image] with the packages in remove left out and the ones in add added. Packages come
    prebuilt (pkg.ARCH_SOURCES), except for the packages that are built from source:
    - Tiny Hat's own packages that the prebuilt repositories lack, and the packages listed in build_from_source,
    - packages with a version pinned in distro.toml, when the prebuilt version is different,
    - packages whose prebuilt ones would pull in something [image] deny names that the x86 image
      does not have (like X11 or SDL2), repeated until nothing new is pulled in.
    The versions and checksums of the prebuilt packages are written to [image-ARCH] in distro.lock."""
    section = f"image-{arch}"
    conf, base = distro.section(section), distro.section("image")
    targets, prefs, skip = pkg.list_config(section)
    dbidx = pkg.arch_db(arch)
    db = dbidx.pkgs
    prefs = {k: v for k, v in prefs.items() if k not in db}
    infos = load()
    by_base = {i["base"]: i for i in infos}
    base_of = {n: i["base"] for i in infos for n in i["pkgnames"]}
    rules = deny_rules()
    name_rules = [r for r in rules if not r[1]]
    x86 = {p["name"] for p in pkg.resolve_targets(base.get("packages", []), indexes(infos), "i686", prefs, skip)}

    local = set(conf.get("build_from_source", []))
    for info in infos:
        if info["sub"] == "tinyhat" and any(n not in db for n in info["pkgnames"]):
            local.add(info["base"])
        # A version pinned in distro.toml is pinned for a reason, so build it if the prebuilt one differs
        pin = distro.tables().get(info["base"], {}).get("version")
        if pin and any(n in db and db[n]["version"] != pin for n in info["pkgnames"]):
            local.add(info["base"])
    local |= {base_of[t] for t in targets if t not in db and t in base_of}

    def resolve():
        both = pkg.Index.new()
        for b in sorted(local):
            for name in by_base[b]["pkgnames"]:
                both.add(name, {"name": name, "base": b, "local": True, "depends": by_base[b]["depends"][name]},
                         by_base[b]["provides"][name])
        for name, p in db.items():
            both.add(name, p)
        for pv, names in dbidx.provides.items():
            for n in names:
                both.link(pv, n)
        sel = pkg.resolve_targets(targets, {arch: both}, arch, prefs, skip)
        names = {p["name"] for p in sel}
        users = {}
        for p in sel:
            for d in p["depends"]:
                users.setdefault(both.resolve(d, prefs, names), []).append(p)
        bad = {p["name"] for p in sel if p["name"] not in x86 and denied(p["name"], None, name_rules)}
        return sel, users, bad

    while True:
        sel, users, bad = resolve()
        more = {base_of[u["name"]] for n in bad for u in users.get(n, [])
                if not u.get("local") and u["name"] not in bad and u["name"] in base_of}
        more |= {base_of[p["name"]] for p in sel if not p.get("local") and PKGCONF.get(base_of.get(p["name"]), {}).get("build_from_source")}
        more -= local
        if not more:
            break
        local |= more

    built = {b: [] for b in conf.get("build_from_source", [])}
    for p in sel:
        if p.get("local"):
            built.setdefault(p["base"], []).append(p["name"])
    tools = package_index(infos, ("arch", "tinyhat"), arches=(arch, "any"))
    todo = [b for b in built if b in by_base]
    while todo:
        for dep in all_deps(by_base[todo.pop()]):
            name = tools.resolve(dep) if dbidx.resolve(dep, prefs) is None else None
            if name is not None and tools.pkgs[name]["base"] not in built:
                info = tools.pkgs[name]
                built[info["base"]] = [n for n in info["pkgnames"] if set(info["arch"][n]) & {arch, "any"}]
                todo.append(info["base"])
    built = {b: sorted(names) for b, names in sorted(built.items())}
    prebuilt = {p["name"]: {"repo": p["repo"], "version": p["version"], "file": p["filename"], "sha256": p["sha256"]}
                for p in sel if not p.get("local")}
    buildroot = arch_buildroot(arch, [by_base[b] for b in built], dbidx, prefs)
    sections = {section: prebuilt, f"buildroot-{arch}": buildroot, f"build-{arch}": built}
    if check:
        stale = [s for s, v in sections.items() if distro.lock().get(s, {}) != v]
        if stale:
            sys.exit(f"buildplan.py: distro.lock {' '.join(f'[{s}]' for s in stale)} no longer matches the TINYBUILDs "
                     f"and distro.toml, run \"make lock-{arch}\"")
        return
    distro.write_lock(**sections)
    print(f"buildplan.py: {section}: {len(prebuilt)} prebuilt packages locked, {len(built)} package directories to build: "
          + " ".join(built), file=sys.stderr)
    print(f"buildplan.py: buildroot-{arch}: {len(buildroot)} prebuilt packages locked", file=sys.stderr)
    for n in sorted(bad):
        print(f"buildplan.py: {section}: {n} ([image] deny) is still pulled in by "
              + " ".join(sorted(u["name"] for u in users.get(n, []))), file=sys.stderr)
    old = sorted(f"{n} {e['version']}" for n, e in prebuilt.items() if denied(n, e["version"], [r for r in rules if r[1]]))
    if old:
        sys.exit(f"buildplan.py: {section}: older than [image] deny allows: {', '.join(old)}")


def arch_buildroot(arch, built, db, prefs):
    """The prebuilt packages of the build environment for another architecture: the [buildroot] packages,
    makepkg and everything the packages built for that architecture need, except what those packages are."""
    own = {n for info in built for n in info["pkgnames"]}
    own |= {pv for info in built for n in info["pkgnames"] for pv in info["provides"][n]}
    br = distro.section("buildroot")
    skip = set(br.get("skip", []))
    wanted = list(SEED_BASE) + list(br.get("packages", [])) + list(distro.section(f"image-{arch}").get("buildroot", []))
    for info in built:
        wanted += [d for d in all_deps(info) if d not in wanted]
    targets = []
    for dep in wanted:
        if dep in own or dep in skip or dep.startswith("lib32-"):
            continue
        if db.resolve(dep, prefs) is None:
            print(f"buildplan.py: buildroot-{arch}: {dep} is not available prebuilt, leaving it out", file=sys.stderr)
            continue
        targets.append(dep)
    sel = pkg.resolve_targets(targets, {arch: db}, arch, prefs, skip)
    return {p["name"]: {"repo": p["repo"], "version": p["version"], "file": p["filename"], "sha256": p["sha256"]}
            for p in sel}


def build_version(info, name):
    """The full version (epoch:pkgver-pkgrel) that a TINYBUILD gives a package."""
    epoch, _, ver = info["version"].partition(":")
    v = f"{ver}-{info['release']}"
    return v if epoch == "0" else f"{epoch}:{v}"


def cmd_pkgfile(repodir, arch, name):
    """The package file that our TINYBUILD builds for NAME, in the version it gives."""
    infos = load()
    by_base = {i["base"]: i for i in infos}
    idx = package_index(infos, ("arch", "tinyhat"))
    pname = idx.resolve(name)
    if pname is None:
        sys.exit(f"buildplan.py: no TINYBUILD makes {name}")
    info = by_base[idx.pkgs[pname]["base"]]
    parch = "any" if "any" in info["arch"][pname] else arch
    path = os.path.join(repodir, f"{pname}-{build_version(info, pname)}-{parch}.pkg.tar.zst")
    if not os.path.exists(path):
        sys.exit(f"buildplan.py: {os.path.basename(path)} is not built yet (packages/{info['base']})")
    print(path)


def cmd_pkgfiles(repodir, arch, names):
    """The package files that our TINYBUILDs build for NAMES and everything they need at run time, the versions
    that they give, never a prebuilt package: for the sysroot of scripts/buildpackage.sh --cross."""
    infos = load()
    by_base = {i["base"]: i for i in infos}
    idx = package_index(infos, ("arch", "tinyhat"))
    seen, todo, out = set(), list(names), []
    while todo:
        dep = todo.pop()
        name = idx.resolve(dep)
        if name is None:
            sys.exit(f"buildplan.py: no TINYBUILD makes {dep}")
        if name in seen:
            continue
        seen.add(name)
        info = by_base[idx.pkgs[name]["base"]]
        ver = build_version(info, name)
        parch = "any" if "any" in info["arch"][name] else arch
        path = os.path.join(repodir, f"{name}-{ver}-{parch}.pkg.tar.zst")
        if not os.path.exists(path):
            sys.exit(f"buildplan.py: {os.path.basename(path)} is not built yet (packages/{info['base']})")
        out.append(path)
        todo += info["depends"].get(name, [])
    print("\n".join(sorted(out)))


def cmd_cross_plan(roots):
    """The package directories that the given packages need at run time, in build order, one per line: the package directory
    and the i686 packages from the same set that its build needs (i686/NAME), for scripts/buildpackage.sh --cross.
    Cycles are broken where a package needs one that is not built yet; build those twice."""
    infos = load()
    by_base = {i["base"]: i for i in infos}
    idx = package_index(infos, ("arch", "tinyhat"))
    need, todo = set(), list(roots)
    while todo:
        name = idx.resolve(todo.pop())
        if name is None or name in need:
            continue
        need.add(name)
        todo += by_base[idx.pkgs[name]["base"]]["depends"].get(name, [])
    bases = {idx.pkgs[n]["base"] for n in need}
    deps = {}
    for b in bases:
        names = set()
        for d in all_deps(by_base[b]):
            n = idx.resolve(d)
            if n and idx.pkgs[n]["base"] in bases and idx.pkgs[n]["base"] != b:
                names.add(n)
        deps[b] = names
    order, built, left = [], set(), set(bases)
    while left:
        ready = sorted(b for b in left if {idx.pkgs[n]["base"] for n in deps[b]} <= built)
        if not ready:
            ready = [min(left, key=lambda b: (len({idx.pkgs[n]["base"] for n in deps[b]} - built), b))]
        for b in ready:
            order.append(b)
            built.add(b)
            left.discard(b)
    for b in order:
        print(by_base[b]["dir"], " ".join(f"i686/{n}" for n in sorted(deps[b])))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "mk"
    if cmd == "mk":
        cmd_mk()
    elif cmd == "seed":
        cmd_seed()
    elif cmd == "libs":
        cmd_libs()
    elif cmd == "pkgfile" and len(sys.argv) == 5:
        cmd_pkgfile(*sys.argv[2:])
    elif cmd == "pkgfiles" and len(sys.argv) > 4:
        cmd_pkgfiles(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif cmd == "cross-plan" and len(sys.argv) > 2:
        cmd_cross_plan(sys.argv[2:])
    elif cmd == "lock-arch" and len(sys.argv) in (3, 4) and sys.argv[3:] in ([], ["--check"]):
        cmd_lock_arch(sys.argv[2], sys.argv[3:] == ["--check"])
    else:
        sys.exit("usage: buildplan.py mk | seed | libs | lock-arch ARCH [--check] | cross-plan PACKAGE... | pkgfile REPODIR ARCH NAME | pkgfiles REPODIR ARCH NAME...")


if __name__ == "__main__":
    main()
