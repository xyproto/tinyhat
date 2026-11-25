#!/usr/bin/env python3
import fnmatch
import json
import os
import re
import sys
import tarfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import distro
import pkg

TOP = pkg.TOP
SEED_BASE = ["base-devel", "pacman", "fakeroot", "git", "ccache", "python", "perl"]
# What runs the builds in the build environment for another architecture, prebuilt even when the image gets its own
# build of it: fish runs makep
RUNS_BUILDS = ["fish"]
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
            "makedepends": get({}, "makedepends"), "makedepends_host": get({}, "makedepends_host"),
            "makedepends_by_arch": {a: base.get("makedepends", []) + base.get(f"makedepends_{a}", [])
                                    for a in ("i686", "x32", "x86_64", "aarch64")},
            "makedepends_build": get({}, "makedepends_build"),
            "bootstrap_without": get({}, "bootstrap_without"),
            "constraints": constraints,
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


def exact_local(wanted, full, tinyhat, skip=frozenset()):
    """The packages of WANTED and what they need at run time (as TINYHAT knows them), without SKIP and what only those
    would bring in (the build environment's own tools)."""
    out = []
    for dep in wanted:
        name = full.resolve(dep)
        if name is not None and name not in out and name not in skip:
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


# The versions of the packages in the i686 build environment (own_seed), for the keys of the builds (tool_versions)
SEED_VERSIONS = {}


def own_seed(infos):
    """The i686 build environment (build/buildroot, scripts/mkbuildroot.sh): [buildroot] i686 and what those need at
    run time, as "make cross" builds them, indexed like a package database (name, base, version, depends). The i686
    builds are planned against it, as they once were against the archlinux32 seed."""
    by_base = {i["base"]: i for i in infos}
    idx = package_index(infos, ("arch", "tinyhat"))
    seed = pkg.Index.new()
    todo = list(distro.section("buildroot").get("i686", []))
    while todo:
        dep = todo.pop()
        name = idx.resolve(dep)
        if name is None:
            sys.exit(f"build_planner.py: no TINYBUILD makes {dep}, which [buildroot] i686 needs")
        if name in seed.pkgs:
            continue
        info = by_base[idx.pkgs[name]["base"]]
        deps = info["depends"].get(name, [])
        seed.add(name, {"name": name, "base": info["base"], "version": build_version(info, name), "depends": deps},
                 info["provides"][name])
        todo += deps
    return seed


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


def prune_declared(plans, full):
    """Leave out the run-time dependencies (depends, not makedepends) that a TINYBUILD says its build can do without
    (bootstrap_without): a build does not need them, so there is no cycle to break with a second build."""
    for b, (info, hard, soft) in list(plans.items()):
        # The package directories named: all their packages (like libwireplumber for wireplumber)
        without = {full.pkgs[n]["base"] for n in (full.resolve(w) for w in info["bootstrap_without"]) if n}
        build = {full.resolve(d) for d in info["makedepends"]}
        drop = {d for d in hard + soft if full.pkgs[d]["base"] in without and d not in build}
        if drop:
            plans[b] = (info, [d for d in hard if d not in drop], [d for d in soft if d not in drop])


def prune_runtime(plans, base_of, idx, prefs):
    """In a dependency cycle, leave out the run-time dependencies (depends, not makedepends) that a build cannot need:
    those on a package for any architecture (data, like ca-certificates), or of a package directory that only makes
    such packages. Other cycles are broken where a TINYBUILD says so (bootstrap_without, native_cycles): a run-time
    dependency is usually linked with, so it is never dropped on a guess."""
    pkgs = idx["i686"][0]
    any_pkg = lambda n: pkgs.get(n, {}).get("arch") == "any"
    graph = {b: {base_of[d] for d in hard} - {b} for b, (_, hard, _) in plans.items()}
    for comp in sccs(graph):
        if len(comp) < 2:
            continue
        for b in comp:
            info, hard, soft = plans[b]
            build = {idx["i686"].resolve(d, prefs) for d in info["makedepends"]}
            data_only = all(any_pkg(n) for n in info["pkgnames"])
            plans[b] = (info, [d for d in hard if base_of[d] not in comp or d in build
                               or not (data_only or any_pkg(d))], soft)


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


def native_cycles(plans, base_of, full, wanted=None):
    """How the dependency cycles that are left among the i686 builds (PLANS: base -> (info, hard, soft)) are broken:
    in each cycle, the package directories are built in order, and one whose dependencies in the cycle are not built
    yet must say that it can do without them (bootstrap_without in its TINYBUILD). It is built first without them
    (i686-boot/BASE), and again with everything after them (i686/BASE). Returns {base: the package directories of the
    cycle that its first build is without}, {(base, dependency): the kind of build of the dependency it needs}, and the
    package directories of cycles that no image needs (WANTED), which are left out instead."""
    graph = {b: {base_of[d] for d in hard + soft} - {b} for b, (_, hard, soft) in plans.items()}
    first, kind, unwanted = {}, {}, set()
    for comp in sccs(graph):
        if len(comp) < 2:
            continue
        if wanted is not None and not comp & wanted:
            unwanted |= comp
            continue
        inner = {b: graph[b] & comp for b in comp}
        can_skip = {b: {base_of[n] for n in (full.resolve(w) for w in plans[b][0]["bootstrap_without"]) if n}
                    for b in comp}
        order, built, left = [], set(), set(comp)
        while left:
            ready = sorted(b for b in left if inner[b] <= built)
            if not ready:
                ready = sorted(b for b in left if inner[b] - built <= can_skip[b])[:1]
            if not ready:
                sys.exit("build_planner.py: i686 dependency cycle with no bootstrap_without to break it: "
                         + " ".join(sorted(b for b in left if inner[b] & left)))
            order += ready
            built |= set(ready)
            left -= set(ready)
        pos = {b: i for i, b in enumerate(order)}
        for b in comp:
            later = {d for d in inner[b] if pos[d] > pos[b]}
            if later:
                first[b] = {d for d in comp if pos[d] > pos[b]}
        for b in comp:
            for d in inner[b]:
                kind[(b, d)] = "i686-boot" if d in first and pos[d] < pos[b] else "i686"
    return first, kind, unwanted


def all_deps(info, arch=None):
    deps = list(info["makedepends_by_arch"][arch] if arch else info["makedepends"])
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
    """The key of a build in the plan, and its stamp build/pkg/KIND/BASE."""
    return f"{kind}/{base}"


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


PLAN = os.path.join(TOP, "build", "plan.json")
NINJA = os.path.join(TOP, "build", "pkgs.ninja")
# The package builds of the plan, "KIND/BASE" -> build (see rule), and the package names that the builds of each
# kind make (for the tool versions)
BUILDS = {}
NAME_BASE = {}


def tool_versions(info, kind, deps, lockname):
    """NAME=VERSION for what the TINYBUILD names (makedepends and depends) that comes from the build environment
    (lockname in distro.lock) and not from the builds it depends on: a new version there rebuilds the package."""
    seeded = SEED_VERSIONS if lockname == "buildroot" else {p["name"]: p["version"] for p in distro.locked(lockname)}
    built = {d.split("/", 1)[1] for d in deps}
    out = set()
    for d in all_deps(info) + list(info.get("checkdepends", [])):
        name = re.split(r"[<>=]", d, maxsplit=1)[0]
        if name in info["pkgnames"] or NAME_BASE.get(kind, {}).get(name) in built:
            continue
        if name in seeded:
            out.add(f"{name}={seeded[name]}")
    return sorted(out)


def rule(key, info, deps, args, name, lockname="buildroot", order=()):
    """A package build: scripts/buildpackage.sh ARGS for the package directory of INFO, after the builds DEPS
    ("KIND/BASE"). Its cache key (buildpackage.sh) is what decides whether it builds; ninja runs it when one of its
    files, a build it depends on (when its packages changed) or its command (the tool versions too) changed. ORDER are
    builds that only have to be there first (shared arch=any packages)."""
    kind = lockname.partition("-")[2] or "i686"
    deps = sorted(set(deps) - {key})
    conf = distro.tables().get(info["base"], {})
    BUILDS[key] = {"kind": key.split("/")[0], "base": info["base"], "name": name, "mode": "built", "dir": info["dir"],
                   "deps": deps, "order": sorted(set(order) - set(deps) - {key}), "args": args,
                   "env": tool_versions(info, kind, deps, lockname),
                   "theme": bool(conf.get("theme")), "heavy": bool(conf.get("heavy"))}


def prebuilt_rule(key, base, names):
    """A package directory taken prebuilt from the seed: NAMES as NAME=VERSION."""
    BUILDS[key] = {"kind": key.split("/")[0], "base": base, "name": key.replace("/", "-"), "mode": "prebuilt",
                   "deps": [], "names": names}


def package_files(d):
    """The files of a package directory that its build depends on (its cache key has them too), not .SRCINFO."""
    out = []
    for root, dirs, files in os.walk(os.path.join(TOP, d)):
        dirs.sort()
        out += [os.path.relpath(os.path.join(root, f), TOP) for f in sorted(files) if f != ".SRCINFO"]
    return out


def ninja_path(p):
    return p.replace("$", "$$").replace(" ", "$ ").replace(":", "$:")


def write_plan(groups, extra):
    """build/plan.json (the plan as data, for the tools) and build/pkgs.ninja (the package builds, run by ninja through
    the Makefile), each written only when it changed."""
    # A build that needs one that is not in the plan (like the x32 glibc, with no x32 builds) cannot be built: leave
    # it out, and what needs it, unless an image needs it
    while True:
        gone = {k: [d for d in b["deps"] if d not in BUILDS] for k, b in BUILDS.items()}
        gone = {k: m for k, m in gone.items() if m}
        if not gone:
            break
        for k, m in sorted(gone.items()):
            if any(k in groups[g] for g in ("rootfs-i686", "rootfs-x32", "cross")):
                sys.exit(f"build_planner.py: {k} is needed, but needs {' '.join(m)}, which is not in the plan")
            print(f"build_planner.py: leaving out {k}: it needs {' '.join(m)}, which is not in the plan", file=sys.stderr)
            del BUILDS[k]
    groups = {g: [k for k in keys if k in BUILDS] for g, keys in groups.items()}
    plan = {"builds": BUILDS, "groups": groups, **extra}
    distro.write_if_changed(PLAN, json.dumps(plan, indent=1, sort_keys=True) + "\n")
    out = ["# The package builds, from scripts/build_planner.py (do not edit): run through the Makefile, as in make cross",
           "ninja_required_version = 1.10",
           "builddir = build",
           "# pool build and pool heavy (one heavy build at a time), from the Makefile (PKG_JOBS)",
           "include build/pools.ninja",
           "",
           "rule pkg",
           "  command = scripts/buildpackage.sh --stamp $out --name $name $args || { tail -n 40 build/log/$name.log; exit 1; }",
           "  description = $name",
           "  restat = 1",
           "",
           "# The stamp of a build or a group is rewritten only when it changed (restat), so that what depends on it",
           "# runs only then",
           "rule prebuilt",
           "  command = python3 scripts/pkg.py prebuilt build/repo vendor/archlinux32/pkg.lock $names && "
           "printf '%s\\n' $names > $out.new && { cmp -s $out.new $out && rm -f $out.new || mv -f $out.new $out; }",
           "  description = $name (prebuilt)",
           "  restat = 1",
           "",
           "# An i686 package directory that \"make cross\" builds: its i686 packages are the ones of the cross build",
           "rule fromcross",
           "  command = cmp -s $in $out || cp -f $in $out",
           "  description = $name (from the cross build)",
           "  restat = 1",
           "",
           "rule group",
           "  command = cat $in > $out.new && { cmp -s $out.new $out && rm -f $out.new || mv -f $out.new $out; }",
           "  description = $out",
           "  restat = 1",
           ""]
    for key in sorted(BUILDS):
        b = BUILDS[key]
        deps = " ".join(f"build/pkg/{d}" for d in b["deps"])
        if b["mode"] == "cross":
            out += [f"build build/pkg/{key}: fromcross {deps}", f"  name = {b['name']}", ""]
            continue
        if b["mode"] == "prebuilt":
            out += [f"build build/pkg/{key}: prebuilt" + (f" | {deps}" if deps else ""),
                    f"  name = {b['name']}", f"  names = {' '.join(b['names'])}", ""]
            continue
        files = " ".join(ninja_path(f) for f in package_files(b["dir"]) + (["distro.toml"] if b["theme"] else []))
        args = (["--env", "'" + " ".join(b["env"]) + "'"] if b["env"] else []) + b["args"]
        order = " ".join(f"build/pkg/{d}" for d in b.get("order", []))
        out += [f"build build/pkg/{key}: pkg {files}" + (f" | {deps}" if deps else "") + (f" || {order}" if order else ""),
                f"  name = {b['name']}",
                "  args = " + " ".join(args).replace("$", "$$"),
                f"  pool = {'heavy' if b['heavy'] else 'build'}", ""]
    for g in sorted(groups):
        out += [f"build build/pkg/group/{g}: group " + " ".join(f"build/pkg/{k}" for k in sorted(groups[g])), ""]
    distro.write_if_changed(NINJA, "\n".join(out))


def keys_for(sel):
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
    # The i686 build environment is our own (own_seed), and nothing in it is broken
    seed = own_seed(infos)
    broken = set()
    SEED_VERSIONS.update({n: p["version"] for n, p in seed.pkgs.items()})
    closures = {}
    versions = {"local": {n: i["version"] for i in infos for n in i["pkgnames"]}, "seed": dict(SEED_VERSIONS)}
    x32_base = {p["name"]: p["base"] for p in idx["x32"][0].values() if p["arch"] == "x32"}
    i686_keys, x32_keys, tinyhat = [], [], []

    targets, lprefs, skip = pkg.list_config("image")
    local_pkgs, local_provides = idx["i686"]
    with_metadata = lambda metadata: pkg.Index(metadata, local_provides)
    as_built = local_pkgs
    rootfs_i686 = pkg.resolve_targets(targets, idx, "i686", lprefs, skip)
    image = {p["name"] for p in rootfs_i686}
    # The i686 packages that "make cross" builds (the Orbiton image, the boot medium and the build environment) are
    # not built again: the i686 build of such a package directory is its cross build (fromcross)
    prebuilt = cross_bases(by_base, cross_roots())[1]
    prebuilt_names = {n for b in prebuilt for n in by_base[b]["pkgnames"]}

    for info in infos:
        for kind in ("i686", "x32", "boot"):
            for n in info["pkgnames"]:
                NAME_BASE.setdefault(kind, {})[n] = info["base"]
        for kind in ("i686", "x32", "boot"):
            for n in info["pkgnames"]:
                for pv in info["provides"][n]:
                    NAME_BASE[kind].setdefault(pv, info["base"])
    i686_plans = {}
    shells = {full.resolve(n) for n in distro.section("buildroot").get("provided", [])} - {None}
    for info in infos:
        arches = base_arches(info)
        if not arches & {"i686", "any"}:
            continue
        own = set(info["pkgnames"])
        soft = []
        if info["base"] in prebuilt:
            target = stamp("i686", info["base"])
            i686_keys.append(target)
            BUILDS[target] = {"kind": "i686", "base": info["base"], "name": f"i686-{info['base']}", "mode": "cross",
                              "deps": [stamp("cross", info["base"])]}
            if info["sub"] == "tinyhat":
                tinyhat += info["pkgnames"]
            continue
        if info["sub"] == "tinyhat":
            tinyhat += info["pkgnames"]
        hard, soft = auto_local(info, seed, idx, prefs, broken, closures, versions, prebuilt_names, image)
        # The shell that runs the builds and the build tools of [buildroot] provided are the build environment's
        # (scripts/mkbuildroot.sh), never a dependency
        deps = [d for d in exact_local(hard, full, full, shells) if d not in own and d not in shells]
        soft = [d for d in exact_local(soft, full, full, shells) if d not in own and d not in deps and d not in shells]
        i686_plans[info["base"]] = (info, deps, soft)
    prune_declared(i686_plans, full)
    prune_runtime(i686_plans, base_of, idx, prefs)
    prune_seeded(i686_plans, base_of, set(seed.pkgs))
    prune_soft(i686_plans, base_of)
    # A dependency cycle among package directories that no image needs is not broken: they are left out
    boot_first, dep_kind, unwanted = native_cycles(i686_plans, base_of, full, needed_package_dirs(infos, keys_for(rootfs_i686)))
    if unwanted:
        print(f"build_planner.py: leaving out {' '.join(sorted(unwanted))}: a dependency cycle that no image needs",
              file=sys.stderr)
        for b in unwanted:
            del i686_plans[b]

    for info, deps, soft in i686_plans.values():
        deps = deps + soft
        b = info["base"]
        if b in boot_first:
            # The first build in a dependency cycle, without what is built after it
            early = [d for d in deps if base_of[d] not in boot_first[b]]
            rule(stamp("i686-boot", b), info, [stamp(dep_kind.get((b, base_of[d]), "i686"), base_of[d]) for d in early],
                 [info["dir"]] + [f"i686/{d}" for d in early], f"i686-boot-{b}")
        args = [info["dir"]] + [f"i686/{d}" for d in deps]
        prereqs = [stamp(dep_kind.get((b, base_of[d]), "i686"), base_of[d]) for d in deps]
        for d in SYSROOT.get(info["base"], []):
            args.append(f"sysroot/{d}")
            prereqs.append(stamp("x32", x32_base[d]))
        target = stamp("i686", info["base"])
        i686_keys.append(target)
        rule(target, info, prereqs, args, f"i686-{info['base']}")

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
            rule(stamp("boot", base), info,
                 i686_pre + [stamp("x32", x32_base[n]) for n in outside],
                 ["--arch", "x32", "--bootstrap", info["dir"]] + i686_args + [f"x32/{n}" for n in outside],
                 f"boot-{base}")
        args, prereqs = ["--arch", "x32", info["dir"]] + i686_args, list(i686_pre)
        for n in x32:
            kind = "boot" if x32_base[n] in comp else "x32"
            args.append(f"{kind}/{n}")
            prereqs.append(stamp(kind, x32_base[n]))
        target = stamp("x32", base)
        x32_keys.append(target)
        rule(target, info, prereqs, args, f"x32-{base}")

    x32_index = idx["x32"]
    if x32_only is not None:
        x32_index = pkg.Index.new()
        for n, p in idx["x32"].pkgs.items():
            if p["arch"] == "any" or p["base"] in x32_only:
                x32_index.add(n, p, idx["x32"].provided_by(n))
    x32_targets, _, x32_skip = pkg.list_config("image-x32") if "image-x32" in distro.load() else (targets, lprefs, skip)
    rootfs_x32 = pkg.resolve_targets(x32_targets, {"i686": with_metadata(as_built), "x32": x32_index}, "x32", lprefs, x32_skip)
    x86_64_keys = arch_rules("x86_64", by_base, prefs)
    aarch64_keys = cross_rules(by_base, aarch64_roots(), "aarch64")
    cross_keys = cross_rules(by_base, cross_roots())
    rootfs_i686, rootfs_x32 = keys_for(rootfs_i686), keys_for(rootfs_x32)
    need = needed_package_dirs(infos, rootfs_i686 + rootfs_x32)
    groups = {"i686": i686_keys, "x32": x32_keys, "aarch64": aarch64_keys, "x86_64": x86_64_keys, "cross": cross_keys,
              "rootfs-i686": rootfs_i686, "rootfs-x32": rootfs_x32,
              "packages": [k for k in i686_keys if k.split("/", 1)[1] in need]}
    # used: the i686 and x32 package directories that the images use, with what builds them; needed: those and
    # everything the TINYBUILDs of the images depend on, for every architecture (for autoupdate.py)
    write_plan(groups, {"tinyhat_pkgs": sorted(tinyhat), "needed": sorted(need),
                        "used": sorted(needed_bases(rootfs_i686 + rootfs_x32))})


def arch_rules(arch, by_base, prefs):
    """Rules for the package directories that "make lock-ARCH" decided to build for another architecture ([build-ARCH] in
    distro.lock). Each one gets the other packages built for that architecture that it needs."""
    built = [by_base[b] for b in sorted(distro.lock().get(f"build-{arch}", {})) if b in by_base]
    here = package_index(built, ("arch", "tinyhat"), arches=())
    names = here.pkgs
    plans = {}
    for info in built:
        own = set(info["pkgnames"])
        plans[info["base"]] = (here.closure(all_deps(info, arch), runtime_deps(here), own),
                               here.closure(info["makedepends_by_arch"][arch], runtime_deps(here), own))
    graph = {b: {names[d]["base"] for d in deps} - {b} for b, (deps, _) in plans.items()}
    for comp in sccs(graph):
        for b in comp if len(comp) > 1 else ():
            deps, make = plans[b]
            plans[b] = ([d for d in deps if names[d]["base"] not in comp or d in make], make)
    seeded = set(distro.lock().get(f"buildroot-{arch}", {}))
    provided = set(distro.section("buildroot").get("provided", []))
    env = seeded | {n for n in names if n in provided or set(names[n]["provides"].get(n, [])) & provided}
    env -= set(distro.section("buildroot").get("prefer_built", []))
    plans = {b: ([d for d in deps if d not in env], make) for b, (deps, make) in plans.items()}
    stamps = []
    for info in built:
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
        rule(target, info, [stamp(arch, names[d]["base"]) for d in deps],
             ["--arch", arch, info["dir"]] + [f"{arch}/{d}" for d in deps], f"{arch}-{info['base']}", f"buildroot-{arch}")
    return stamps


def needed_bases(roots):
    """The package directories (i686 and x32) that the images and the Makefile need (ROOTS: keys of the builds of the
    root file systems), with the builds it takes to build them."""
    deps = {}
    for key, b in BUILDS.items():
        if b["kind"] in ("i686", "x32", "boot"):
            deps.setdefault(b["base"], set()).update(d.split("/", 1)[1] for d in b["deps"])
    roots = {k.split("/", 1)[1] for k in roots}
    with open(os.path.join(TOP, "Makefile")) as f:
        roots |= set(re.findall(r"pkg/(?:i686|x32)/([a-z0-9][A-Za-z0-9@._+-]*)", f.read()))
    need, todo = set(), list(roots)
    while todo:
        b = todo.pop()
        if b not in need:
            need.add(b)
            todo += deps.get(b, ())
    return need


def needed_package_dirs(infos, roots):
    """The package directories needed for the images: the ones the images and the Makefile use (needed_bases) and the
    aarch64 and x86_64 builds, and everything their TINYBUILDs depend on, at build time or at run time, whether or not
    that ends up built here."""
    full = package_index(infos, ("arch", "tinyhat"))
    by_base = {i["base"]: i for i in infos}
    need = needed_bases(roots) | set(distro.lock().get("build-aarch64", {})) | set(distro.lock().get("build-x86_64", {}))
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
    p = distro.plan()
    used = set(p["used"]) if p else None
    local = package_index(infos)
    db = seed_index()
    idx = indexes(infos)
    prefs = distro.providers()
    seed = list(SEED_BASE)
    excluded, wanted_by = distro.excluded(), {}
    # A package that is taken prebuilt is never built, so what it takes to build it is not needed
    prebuilt = {b["base"] for b in p["builds"].values() if b["mode"] == "prebuilt"} if p else set()
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
        sys.exit("build_planner.py: TINYBUILDs need excluded packages in the build environment:\n" +
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
    """Lock what the image for another architecture ([image-ARCH] in distro.toml) builds, and its build environment.

    The image is [image] with the packages in remove left out and the ones in add added, all built from our TINYBUILDs
    for ARCH, like the i686 image. Only the build environment comes prebuilt (pkg.ARCH_SOURCES). Writes the package
    directories to build ([build-ARCH]) and the prebuilt build environment ([buildroot-ARCH]) to distro.lock."""
    section = f"image-{arch}"
    targets, prefs, skip = pkg.list_config(section)
    dbidx = pkg.arch_db(arch)
    infos = load()
    by_base = {i["base"]: i for i in infos}
    own = pkg.Index.new()
    for info in infos:
        for name in info["pkgnames"]:
            if set(info["arch"][name]) & {arch, "i686", "any"}:
                own.add(name, {"name": name, "arch": arch, "base": info["base"], "sub": info["sub"],
                               "depends": info["depends"][name]}, info["provides"][name])
    missing = [t for t in targets if own.resolve(t, prefs) is None]
    if missing:
        sys.exit(f"build_planner.py: {section}: no TINYBUILD makes {' '.join(missing)}")
    sel = pkg.resolve_targets(targets, {arch: own}, arch, prefs, skip)
    prefs = {k: v for k, v in prefs.items() if k not in dbidx.pkgs}

    built = {}
    # The x86_64 build environment builds the i686 and aarch64 packages too, with the cross toolchains (cross_rules)
    if arch == "x86_64":
        built["i686-cross"] = []
        built["aarch64-cross"] = []
    for p in sel:
        built.setdefault(p["base"], []).append(p["name"])
    for b in distro.section(section).get("boot", []):
        built.setdefault(b, [])
    tools = package_index(infos, ("arch", "tinyhat"), arches=(arch, "any"))

    def build_tools(deps):
        """Build for this architecture what DEPS need that is not available prebuilt; the package directories that
        are added."""
        added = []
        for dep in deps:
            name = tools.resolve(dep) if dbidx.resolve(dep, prefs) is None else None
            if name is not None and tools.pkgs[name]["base"] not in built:
                info = tools.pkgs[name]
                built[info["base"]] = [n for n in info["pkgnames"] if set(info["arch"][n]) & {arch, "any"}]
                added.append(info["base"])
        return added

    todo = [b for b in built if b in by_base]
    # The tools that the i686 cross builds run (like metatar and mtools), when no prebuilt package has them
    if arch == "x86_64":
        todo += build_tools(cross_tools(by_base))
    while todo:
        todo += build_tools(all_deps(by_base[todo.pop()], arch))
    built = {b: sorted(names) for b, names in sorted(built.items())}
    buildroot = arch_buildroot(arch, [by_base[b] for b in built], dbidx, prefs,
                               cross_tools(by_base) if arch == "x86_64" else [])
    sections = {section: {}, f"buildroot-{arch}": buildroot, f"build-{arch}": built}
    if check:
        stale = [s for s, v in sections.items() if distro.lock().get(s, {}) != v]
        if stale:
            sys.exit(f"build_planner.py: distro.lock {' '.join(f'[{s}]' for s in stale)} no longer matches the TINYBUILDs "
                     f"and distro.toml, run \"make lock-{arch}\"")
        return
    distro.write_lock(**sections)
    pkg.prune(arch)
    print(f"build_planner.py: {section}: {len(built)} package directories to build: " + " ".join(built), file=sys.stderr)
    print(f"build_planner.py: buildroot-{arch}: {len(buildroot)} prebuilt packages locked", file=sys.stderr)


def arch_buildroot(arch, built, db, prefs, tools=()):
    """The prebuilt packages of the build environment for another architecture: the [buildroot] packages,
    makepkg and everything the packages built for that architecture need, except what those packages are."""
    own = {n for info in built for n in info["pkgnames"]}
    own |= {pv for info in built for n in info["pkgnames"] for pv in info["provides"][n]}
    br = distro.section("buildroot")
    skip = set(br.get("skip", []))
    wanted = list(SEED_BASE) + list(br.get("packages", [])) + list(distro.section(f"image-{arch}").get("buildroot", []))
    always = set(wanted) | set(tools)
    for info in built:
        wanted += [d for d in all_deps(info, arch) if d not in wanted]
    # And what the i686 cross builds run (cross_tools)
    wanted += [d for d in tools if d not in wanted]
    targets = list(RUNS_BUILDS)
    for dep in wanted:
        if (dep in own and dep not in always) or dep in skip or dep.startswith("lib32-") or dep in targets:
            continue
        if db.resolve(dep, prefs) is None:
            print(f"build_planner.py: buildroot-{arch}: {dep} is not available prebuilt, leaving it out", file=sys.stderr)
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
        sys.exit(f"build_planner.py: no TINYBUILD makes {name}")
    info = by_base[idx.pkgs[pname]["base"]]
    parch = "any" if "any" in info["arch"][pname] else arch
    path = os.path.join(repodir, f"{pname}-{build_version(info, pname)}-{parch}.pkg.tar.zst")
    if not os.path.exists(path):
        sys.exit(f"build_planner.py: {os.path.basename(path)} is not built yet (packages/{info['base']})")
    print(path)


def toolchain_provided(idx, arch="i686"):
    """The packages that a cross build does not get from our builds: shells (the build environment has its own) and
    gcc's packages (the cross toolchain has the compiler and its run-time libraries, libgcc, libstdc++...). For
    aarch64 also glibc's and the kernel headers' (the aarch64-cross sysroot has them; i686 builds use our own)."""
    shells = {idx.resolve(n) for n in ("bash", "sh")} - {None}
    bases = ("gcc",) if arch == "i686" else ("gcc", "glibc", "linux-api-headers")
    return lambda name: name in shells or idx.pkgs[name]["base"] in bases


def cmd_pkgfiles(repodir, arch, names, without=(), image=False):
    """The package files that our TINYBUILDs build for NAMES and everything they need at run time, the versions
    that they give, never a prebuilt package: for the sysroot of scripts/buildpackage.sh --cross, where shells and
    gcc's run-time libraries come along only when named (toolchain_provided), or with IMAGE for a root file system
    (scripts/mkimg-orbiton.sh), which has no toolchain and gets all of them. Packages of the package directories in
    WITHOUT are left out, for the first build of a package in a dependency cycle. For a sysroot, a dependency of a
    dependency that is not built yet is left out too: the plan builds every other one first, so it is one that a
    dependency cycle needs only at run time (like mesa for libglvnd)."""
    infos = load()
    by_base = {i["base"]: i for i in infos}
    idx = package_index(infos, ("arch", "tinyhat"))
    provided = (lambda name: False) if image else toolchain_provided(idx, arch)
    seen, todo, out = set(), [(n, True) for n in names], []
    while todo:
        dep, named = todo.pop()
        name = idx.resolve(dep)
        if name is None:
            sys.exit(f"build_planner.py: no TINYBUILD makes {dep}")
        if name in seen or idx.pkgs[name]["base"] in without or (not named and provided(name)):
            continue
        seen.add(name)
        info = by_base[idx.pkgs[name]["base"]]
        ver = build_version(info, name)
        parch = "any" if "any" in info["arch"][name] else arch
        path = os.path.join(repodir, f"{name}-{ver}-{parch}.pkg.tar.zst")
        if not os.path.exists(path):
            if not image and not named:
                continue
            sys.exit(f"build_planner.py: {os.path.basename(path)} is not built yet (packages/{info['base']})")
        out.append(path)
        todo += [(d, False) for d in info["depends"].get(name, [])]
    print("\n".join(sorted(out)))


def cross_bases(by_base, roots, arch="i686"):
    """The package index of all TINYBUILDs, and the package directories that ROOTS need for i686 (cross_rules)."""
    idx = package_index(list(by_base.values()), ("arch", "tinyhat"))
    need, todo = set(), list(roots)
    while todo:
        name = idx.resolve(todo.pop())
        if name is None or name in need:
            continue
        need.add(name)
        info = by_base[idx.pkgs[name]["base"]]
        todo += info["makedepends_host"]
        if arch != "i686":
            todo += [d for d in info["makedepends"] if d not in info["makedepends_build"] and is_library(d, idx, by_base)]
        for split in info["depends"]:
            todo += info["depends"][split] + [split]
    return idx, {idx.pkgs[n]["base"] for n in need}


def aarch64_roots():
    """What the aarch64 cross builds are for: the packages of the package directories in [build-aarch64], and the
    package directories there without image packages (like linux-tinyhat and grub, which the Makefile uses)."""
    return sorted({n for base, names in distro.lock().get("build-aarch64", {}).items() for n in (names or [base])})


def cross_roots():
    """What the i686 cross builds are for: [image-orbiton] packages (its root file system) and boot (the boot medium),
    and [buildroot] i686 (the i686 build environment, scripts/mkbuildroot.sh), where everything else is built."""
    conf = distro.section("image-orbiton")
    return conf.get("packages", []) + conf.get("boot", []) + distro.section("buildroot").get("i686", [])


def cross_tools(by_base):
    """What the cross builds of [image-orbiton] run during the build, from the x86_64 build environment: their
    makedepends, without the libraries they are built against (makedepends_host)."""
    _, bases = cross_bases(by_base, cross_roots())
    _, more = cross_bases(by_base, aarch64_roots(), "aarch64")
    bases = bases | more
    out = []
    for b in sorted(bases):
        info = by_base[b]
        out += [d for d in info["makedepends"] if d not in info["makedepends_host"] and d not in out]
    return out


LIBRARY_FILE = re.compile(r"usr/(include/|lib/pkgconfig/|lib/cmake/|lib/lib[^/]*\.(so|a)($|\.))")
DATA_PC_FILE = re.compile(r"usr/share/pkgconfig/([^/]+)\.pc$")
PROGRAM_FILE = re.compile(r"usr/bin/([^/]+)$")
_library_cache = {}


def is_library(name, idx, by_base):
    """Whether package NAME is something to build against (headers, pkg-config or CMake files, libraries), from the
    files of our i686 build of it or of the x86_64 build environment's copy; build tools without those run from the
    x86_64 build environment instead. A pkg-config file in usr/share/pkgconfig is built against (directories and data,
    like systemd's or wayland-protocols'), unless it is named after one of the package's programs (scdoc.pc for
    usr/bin/scdoc only finds the program). Unknown packages count as libraries."""
    if name in _library_cache:
        return _library_cache[name]
    files = None
    resolved = idx.resolve(name)
    if resolved is not None:
        info = by_base[idx.pkgs[resolved]["base"]]
        for parch in ("i686", "any"):
            path = os.path.join(TOP, "build", "repo", f"{resolved}-{build_version(info, resolved)}-{parch}.pkg.tar.zst")
            if os.path.exists(path):
                with tarfile.open(path) as t:
                    files = t.getnames()
                break
    if files is None:
        local = os.path.join(TOP, "build", "buildroot-x86_64", "var", "lib", "pacman", "local")
        for d in sorted(os.listdir(local)) if os.path.isdir(local) else []:
            if d.rsplit("-", 2)[0] == name and os.path.exists(os.path.join(local, d, "files")):
                with open(os.path.join(local, d, "files")) as f:
                    files = f.read().split("\n")
                break
    if files is not None:
        files = [f.lstrip("./") for f in files]
    if files is None:
        _library_cache[name] = True
        return True
    programs = {m.group(1) for m in map(PROGRAM_FILE.match, files) if m}
    data_pc = {m.group(1) for m in map(DATA_PC_FILE.match, files) if m}
    _library_cache[name] = any(LIBRARY_FILE.match(f) for f in files) or bool(data_pc - programs)
    return _library_cache[name]


def cross_rules(by_base, roots, arch="i686"):
    """Rules for the i686 packages that ROOTS ([image-orbiton] packages) need, built in the x86_64 build environment
    with the i686-cross toolchain (scripts/buildpackage.sh --cross), from our TINYBUILDs only.

    A package directory builds all its packages, so what any of them needs at run time is needed too, and so are the
    libraries it is built against (makedepends_host); build tools come from the x86_64 build environment. What a
    cross build gets from our builds: the packages of the set that its TINYBUILD names, without the ones that the
    toolchain provides (toolchain_provided). A dependency cycle (like util-linux and systemd) is broken where a TINYBUILD
    says that its build can do without a dependency (bootstrap_without): that package directory is built twice, first
    without the package directories of the cycle that are not built yet ($(B)/pkg/cross-boot/BASE), then with
    everything ($(B)/pkg/cross/BASE)."""
    idx, bases = cross_bases(by_base, roots, arch)
    provided = toolchain_provided(idx, arch)
    deps, graph, soft, declared = {}, {}, {}, {}
    for b in bases:
        # Not makedepends_build: those run during the build, and come from the x86_64 build environment
        info = by_base[b]
        if arch == "i686":
            wanted = [d for d in all_deps(info) if d not in info["makedepends_build"]]
            make = set()
        else:
            # Other architectures build against the libraries among the makedepends; the rest are tools from the x86_64
            # build environment, and what is only needed at run time can come later in a cycle
            libs = [d for d in info["makedepends"] if d not in info["makedepends_build"]
                    and (d in info["makedepends_host"] or is_library(d, idx, by_base))]
            wanted = libs + [d for n in info["pkgnames"] for d in info["depends"].get(n, [])]
            make = {idx.resolve(d) for d in libs} - {None}
        names = {idx.resolve(d) for d in wanted} - {None}
        deps[b] = sorted(n for n in names if idx.pkgs[n]["base"] in bases and idx.pkgs[n]["base"] != b and not provided(n))
        graph[b] = {idx.pkgs[n]["base"] for n in deps[b]}
        without = {idx.resolve(w) for w in by_base[b]["bootstrap_without"]}
        declared[b] = {d for d in graph[b] if all(n in without for n in deps[b] if idx.pkgs[n]["base"] == d)}
        if arch != "i686":
            without |= {n for n in deps[b] if n not in make}
        soft[b] = {d for d in graph[b] if all(n in without for n in deps[b] if idx.pkgs[n]["base"] == d)}
    # Build order: what is ready first; in a cycle, a package directory that can do without the rest, preferably one
    # whose TINYBUILD says so (bootstrap_without), else one that only needs the rest at run time
    order, built, left = [], set(), set(bases)
    while left:
        ready = sorted(b for b in left if graph[b] <= built)
        if not ready:
            ready = sorted(b for b in left if graph[b] - built <= declared[b])[:1]
        if not ready:
            ready = sorted(b for b in left if graph[b] - built <= soft[b])[:1]
        if not ready:
            cycle = sorted(b for b in left if graph[b] & left)
            sys.exit("build_planner.py: dependency cycle with no bootstrap_without to break it: " + " ".join(cycle))
        for b in ready:
            order.append(b)
            built.add(b)
            left.discard(b)
    pos = {b: i for i, b in enumerate(order)}
    boot = {b for b in bases if any(pos[d] > pos[b] for d in graph[b])}
    # Build tools that we build for x86_64 ([build-x86_64]) and that the x86_64 build environment has no prebuilt copy
    # of (like mtools and metatar): a cross build gets those it names as x86_64 packages, and the i686-cross toolchain
    seeded = {p["name"] for p in distro.locked("buildroot-x86_64")}
    x86_names = {n: b["base"] for k, b in BUILDS.items() if b["kind"] == "x86_64" for n in by_base[b["base"]]["pkgnames"]
                 if n not in seeded}
    main, early = ("cross", "cross-boot") if arch == "i686" else (arch, f"{arch}-boot")
    # Package directories with only arch=any packages that a native plan builds: their packages are the same files
    native = {}
    for nb in BUILDS.values():
        if nb["kind"] in ("i686", "x86_64") and native.get(nb["base"]) != "x86_64":
            native[nb["base"]] = nb["kind"]
    shared = {b: native[b] for b in bases if b in native
              and all("any" in by_base[b]["arch"][n] for n in by_base[b]["pkgnames"])}
    # Waiting for such a build only matters while its files are not built yet
    have = {b for b in shared if all(os.path.exists(os.path.join(TOP, "build", "repo",
            f"{n}-{build_version(by_base[b], n)}-any.pkg.tar.zst")) for n in by_base[b]["pkgnames"])}
    if arch == "i686":
        shared = {}

    def kind_of(d, b):
        if d in shared:
            return (shared[d], d)
        return (early if d in boot and pos[d] < pos[b] else main, d)
    for b in order:
        info = by_base[b]
        for n in info["pkgnames"]:
            NAME_BASE.setdefault(main, {})[n] = b
    # Each build: (target, package directory, i686 packages, the stamps of the package directories that build them)
    builds = []
    for b in order:
        if b in shared:
            continue
        if b in boot:
            later = {d for d in graph[b] if pos[d] > pos[b]}
            first = [n for n in deps[b] if idx.pkgs[n]["base"] not in later]
            builds.append(((early, b), b, first, [(shared[d], d) if d in shared else (early if d in boot else main, d)
                                                  for d in sorted({idx.pkgs[n]["base"] for n in first})
                                                  if d not in have]))
        pre = [kind_of(d, b) for d in sorted(graph[b])]
        builds.append(((main, b), b, deps[b], pre + ([(early, b)] if b in boot else [])))
    # The package directories that are surely built before each build (its prerequisites, transitively)
    prereqs = {t: p for t, _, _, p in builds}
    done = {}

    def before(t):
        if t not in done:
            done[t] = set()
            for p in prereqs.get(t, []):
                done[t] |= {p[1]} | before(p)
        return done[t]

    cycle_of = {b: comp for comp in sccs(graph) for b in comp}
    stamps = []
    for target, b, names, pre in builds:
        info = by_base[b]
        names_here = [re.split(r"[<>=]", d, maxsplit=1)[0] for d in info["makedepends"] if d not in info["makedepends_host"]]
        host = [f"{arch}-cross"] + sorted({n for n in names_here if n in x86_names} - {f"{arch}-cross"})
        host_args = [f"x86_64/{t}" for t in host]
        host_pre = sorted({stamp("x86_64", x86_names[t]) for t in host if t in x86_names})
        # Tool versions for the build tools only: the i686 dependencies are builds of their own
        tool_info = dict(info, depends={}, makedepends=[d for d in info["makedepends"] if d not in info["makedepends_host"]])
        # Leave out the package directories of the cycle that are not surely built yet: what the packages here need
        # at run time can lead to them (pam, built first without systemd, still names systemd-libs)
        without = sorted(cycle_of[b] - before(target) - {b})
        args = ["--cross"] + (["--arch", arch] if arch != "i686" else []) + \
            (["--without", ",".join(without)] if without else []) + [info["dir"]]
        kind = target[0]
        if kind == main:
            stamps.append(stamp(*target))
        hard = [p for p in pre if shared.get(p[1]) != p[0] or p[1] not in have]
        rule(stamp(*target), tool_info, host_pre + [stamp(*p) for p in hard],
             args + host_args + [f"{arch}/{n}" for n in names], f"{kind}-{b}", "buildroot-x86_64")
    return stamps


def provided_bases():
    """The package directories of [buildroot] provided: the build environment has those tools from /host."""
    infos = load()
    idx = package_index(infos, ("arch", "tinyhat"))
    names = {idx.resolve(n) for n in distro.section("buildroot").get("provided", [])} - {None}
    return sorted({idx.pkgs[n]["base"] for n in names})


def cmd_query(what):
    """Facts of build/plan.json for shell scripts, one per line: tinyhat-pkgs (the packages of Tiny Hat's own
    TINYBUILDs), built-pkgnames (the packages of the i686 package directories that are built, not taken prebuilt),
    needed (the package directories the images need), provided-bases (what [buildroot] provided names)."""
    p = distro.plan()
    if p is None:
        sys.exit("build_planner.py: build/plan.json is missing, run make first")
    if what == "tinyhat-pkgs":
        out = p["tinyhat_pkgs"]
    elif what == "built-pkgnames":
        by_base = {i["base"]: i for i in load()}
        out = sorted({n for b in p["builds"].values() if b["kind"] == "i686" and b["mode"] in ("built", "cross")
                      and b["base"] in by_base for n in by_base[b["base"]]["pkgnames"]})
    elif what == "needed":
        out = p["needed"]
    elif what == "provided-bases":
        out = provided_bases()
    else:
        sys.exit(f"build_planner.py: unknown query {what}")
    print("\n".join(out))


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
    elif cmd == "pkgfiles" and len(sys.argv) > 7 and sys.argv[2:4] == ["--image", "--without"]:
        cmd_pkgfiles(sys.argv[5], sys.argv[6], sys.argv[7:], without=set(sys.argv[4].split(",")), image=True)
    elif cmd == "pkgfiles" and len(sys.argv) > 5 and sys.argv[2] == "--image":
        cmd_pkgfiles(sys.argv[3], sys.argv[4], sys.argv[5:], image=True)
    elif cmd == "pkgfiles" and len(sys.argv) > 6 and sys.argv[2] == "--without":
        cmd_pkgfiles(sys.argv[4], sys.argv[5], sys.argv[6:], without=set(sys.argv[3].split(",")))
    elif cmd == "pkgfiles" and len(sys.argv) > 4:
        cmd_pkgfiles(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif cmd == "query" and len(sys.argv) == 3:
        cmd_query(sys.argv[2])
    elif cmd == "lock-arch" and len(sys.argv) in (3, 4) and sys.argv[3:] in ([], ["--check"]):
        cmd_lock_arch(sys.argv[2], sys.argv[3:] == ["--check"])
    else:
        sys.exit("usage: build_planner.py mk | seed | libs | query tinyhat-pkgs|built-pkgnames|needed | lock-arch ARCH [--check] | pkgfile REPODIR ARCH NAME | pkgfiles [--image] [--without BASE,...] REPODIR ARCH NAME...")


if __name__ == "__main__":
    main()
