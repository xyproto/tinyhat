#!/usr/bin/env python3
import os
import posixpath
import re
import shlex
import subprocess
import sys

SCALARS = ["pkgname", "pkgver", "pkgrel", "epoch", "pkgdesc", "url", "install", "changelog"]
ARRAYS = ["license", "arch", "groups", "depends", "makedepends", "checkdepends", "optdepends", "provides", "conflicts",
          "replaces", "backup", "options", "source", "noextract", "validpgpkeys", "sha256sums", "b2sums", "sha512sums",
          "md5sums", "sha1sums", "sha224sums", "sha384sums", "cksums"]
FUNCS = ["prepare", "build", "check", "package", "pkgver"]
TOOLS = {"meson", "ninja", "cmake", "make", "pkgconf", "pkg-config", "python", "perl", "git", "go", "go-tinyhat", "rust",
         "cargo", "nasm", "yasm", "bison", "flex", "gperf", "help2man", "doxygen", "graphviz", "asciidoc", "asciidoctor",
         "xmlto", "docbook-xsl", "docbook-xml", "gettext", "intltool", "autoconf", "automake", "libtool",
         "autoconf-archive", "vala", "scdoc", "gzip", "unzip", "zip", "patch", "ruby", "texinfo", "sassc", "gtk-doc",
         "xxd", "tinyxxd", "nodejs", "npm", "glslang", "shaderc", "cc65", "clang", "lld", "mandoc",
         "groff", "sed", "grep", "gawk", "coreutils", "bc", "cpio", "xz", "zstd", "lzip", "bzip2", "tar", "curl", "wget",
         "nuitka", "swig", "rsync", "dejagnu"}
TOOL_PREFIXES = ("python-", "perl-", "ruby-", "rust-", "jdk", "java-", "texlive-")
DUMP = r'''
source ./PKGBUILD >/dev/null 2>&1 || exit 3
for v in SCALARS; do [ -n "${!v+x}" ] && printf 'S\0%s\0%s\0' "$v" "${!v}"; done
for v in ARRAYS $(compgen -A variable | grep -E '^(depends|makedepends|optdepends|provides|conflicts|source|sha256sums|b2sums|md5sums)_[a-z0-9_]+$'); do
  declare -n ref=$v 2>/dev/null || continue
  if [ -n "${ref+x}" ]; then printf 'A\0%s\0%d\0' "$v" "${#ref[@]}"; printf '%s\0' "${ref[@]}"; fi
  unset -n ref
done
for v in $(compgen -A variable | grep -E '^_[A-Za-z0-9_]+$'); do printf 'S\0%s\0%s\0' "$v" "${!v}"; done
for f in FUNCS; do declare -f "$f" >/dev/null && { printf 'F\0%s\0' "$f"; declare -f "$f"; printf '\0'; }; done
exit 0
'''


class NotSimple(Exception):
    pass


def dump(dirpath):
    script = DUMP.replace("SCALARS", " ".join(SCALARS)).replace("ARRAYS", " ".join(ARRAYS)).replace("FUNCS", " ".join(FUNCS))
    env = {"PATH": "/usr/bin:/bin", "CARCH": "i686", "LANG": "C"}
    out = subprocess.run(["bash", "-c", script], cwd=dirpath, env=env, capture_output=True, timeout=30)
    if out.returncode == 3:
        raise NotSimple("PKGBUILD could not be sourced")
    parts = out.stdout.decode("utf-8", "replace").split("\0")
    data, arrays, funcs = {}, {}, {}
    i = 0
    while i < len(parts) - 1:
        kind = parts[i]
        if kind == "S":
            data[parts[i + 1]] = parts[i + 2]
            i += 3
        elif kind == "A":
            n = int(parts[i + 2])
            arrays[parts[i + 1]] = parts[i + 3:i + 3 + n]
            i += 3 + n
        elif kind == "F":
            funcs[parts[i + 1]] = parts[i + 2]
            i += 3
        else:
            i += 1
    return data, arrays, funcs


def body_lines(fn):
    lines = fn.strip().split("\n")[2:-1]
    out = []
    for line in lines:
        line = line.strip().rstrip(";").strip()
        if line:
            out.append(line)
    return out


def tokens(line, subst, arrays=None):
    for name, items in (arrays or {}).items():
        joined = " ".join(shlex.quote(x) for x in items)
        line = line.replace('"${' + name + '[@]}"', joined).replace("${" + name + "[@]}", joined)
    for bad in ("&&", "||", "|", "$(", "`", ">", "<", "for ", "if ", "while ", "case "):
        if bad in line:
            raise NotSimple(f"shell construct in: {line}")
    for k, v in subst.items():
        line = line.replace("${" + k + "}", v).replace("$" + k, v)
    if "$" in line:
        raise NotSimple(f"unresolved variable in: {line}")
    try:
        return shlex.split(line)
    except ValueError:
        raise NotSimple(f"cannot parse: {line}")


def norm(path):
    p = posixpath.normpath(path)
    return "" if p == "." else p


class Func:
    def __init__(self, lines, subst):
        self.items = []
        self.arrays = {}
        cwd = ""
        for line in lines:
            m = re.fullmatch(r"(?:local\s+)?([A-Za-z_]\w*)(\+?)=\((.*)\)", line)
            if m:
                items = tokens(m.group(3), subst, self.arrays) if m.group(3).strip() else []
                self.arrays[m.group(1)] = (self.arrays.get(m.group(1), []) if m.group(2) else []) + items
                continue
            t = tokens(line, subst, self.arrays)
            if t and t[0] == "cd":
                if len(t) != 2:
                    raise NotSimple(f"cd: {line}")
                target = t[1]
                if target.startswith("@SRCDIR@"):
                    target = target[len("@SRCDIR@"):].lstrip("/")
                if target.startswith("/"):
                    raise NotSimple(f"cd to {target}")
                cwd = norm(posixpath.join(cwd, target))
                continue
            self.items.append((cwd, t))


def split_env(t):
    env = {}
    while t and re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", t[0]):
        k, v = t[0].split("=", 1)
        env[k] = v
        t = t[1:]
    return env, t


def is_pkgdir(v):
    return v in ("@PKGDIR@", "@PKGDIR@/")


def parse_install(cwd, t, build_dir, name):
    mode, target_dir, files = "755", None, []
    i = 1
    while i < len(t):
        a = t[i]
        if a.startswith("--mode="):
            mode = a.split("=", 1)[1]
        elif a.startswith("--target-directory="):
            target_dir = a.split("=", 1)[1]
        elif a.startswith("-") and len(a) > 1 and not a.startswith("--"):
            flags = a[1:]
            j = 0
            while j < len(flags):
                c = flags[j]
                if c == "m":
                    rest = flags[j + 1:]
                    if rest:
                        mode = rest
                    else:
                        i += 1
                        mode = t[i]
                    break
                if c == "t":
                    rest = flags[j + 1:]
                    if rest:
                        target_dir = rest
                    else:
                        i += 1
                        target_dir = t[i]
                    break
                if c not in "Dvp":
                    raise NotSimple(f"install flag -{c}")
                j += 1
        elif a.startswith("--"):
            raise NotSimple(f"install option {a}")
        else:
            files.append(a)
        i += 1
    if not re.fullmatch(r"0?[0-7]{3}", mode):
        raise NotSimple(f"install mode {mode}")
    mode = mode[-3:]
    if target_dir is not None:
        dest, srcs, to_dir = target_dir, files, True
    else:
        if len(files) < 2:
            raise NotSimple("install without a destination")
        dest, srcs = files[-1], files[:-1]
        to_dir = dest.endswith("/") or len(srcs) > 1
    if not dest.startswith("@PKGDIR@/"):
        raise NotSimple(f"install outside pkgdir: {dest}")
    dest = dest[len("@PKGDIR@/"):]
    out = []
    for s in srcs:
        if s.startswith("@SRCDIR@/"):
            full = s[len("@SRCDIR@/"):]
        elif s.startswith("/") or "@" in s:
            raise NotSimple(f"install source {s}")
        else:
            full = norm(posixpath.join(cwd, s))
        rel = posixpath.relpath(full, build_dir or ".")
        lic_dir = f"usr/share/licenses/{name}"
        d = dest.rstrip("/")
        if (to_dir and d == lic_dir) or (not to_dir and posixpath.dirname(d) == lic_dir and posixpath.basename(d) == posixpath.basename(rel)):
            out.append(("license", rel, None, None))
        elif to_dir:
            out.append(("install", rel, d + "/", mode))
        else:
            out.append(("install", rel, d, mode))
    return out


MESON_VALUE_OPTS = {"--prefix", "--buildtype", "--libdir", "--libexecdir", "--sbindir", "--bindir", "--sysconfdir",
                    "--localstatedir", "--wrap-mode", "--default-library", "--optimization", "--backend", "--auto-features"}


def meson_args(args):
    pos, opts = [], []
    i = 0
    while i < len(args):
        a = args[i]
        if a.startswith("-"):
            if a in MESON_VALUE_OPTS and i + 1 < len(args):
                opts.append(a + "=" + args[i + 1])
                i += 2
                continue
            if a == "-D" and i + 1 < len(args):
                opts.append("-D" + args[i + 1])
                i += 2
                continue
            opts.append(a)
        else:
            pos.append(a)
        i += 1
    return pos, opts


def order_dirs(pos, used):
    a, b = pos
    if a in used and b not in used:
        return a, b
    if b in used and a not in used:
        return b, a
    names = ("build", "_build", "builddir", "output", "out")
    if posixpath.basename(b) in names and posixpath.basename(a) not in names:
        return b, a
    return a, b


def convert_build(build, package, prepare):
    used = set()
    for _, t in build.items + package.items:
        if "-C" in t and t.index("-C") + 1 < len(t):
            used.add(t[t.index("-C") + 1])
        if "--build" in t and t.index("--build") + 1 < len(t):
            used.add(t[t.index("--build") + 1])
        if "--install" in t and t.index("--install") + 1 < len(t):
            used.add(t[t.index("--install") + 1])
    style, build_dir, configure, make_args, install_args, patches = None, None, [], [], [], []
    plain_meson = False
    builddir_name = None
    for cwd, t in (prepare.items if prepare else []):
        if t[0] == "patch":
            if not any(a in ("-p1", "-Np1", "-Np1i") for a in t):
                raise NotSimple("patch without -p1")
            pt = [a for a in t[1:] if not a.startswith("-")]
            if len(pt) != 1:
                raise NotSimple(f"patch: {t}")
            p = pt[0]
            if p.startswith("@SRCDIR@/"):
                p = p[len("@SRCDIR@/"):]
            else:
                p = norm(posixpath.join(cwd, p))
            if "/" in p:
                raise NotSimple(f"patch outside srcdir: {p}")
            patches.append((cwd, p))
        else:
            raise NotSimple(f"prepare runs {t[0]}")
    for cwd, t in build.items:
        env, t = split_env(t)
        if env:
            raise NotSimple(f"environment in build: {env}")
        if t[0] == "arch-meson":
            pos, opts = meson_args(t[1:])
            if len(pos) != 2:
                raise NotSimple("arch-meson arguments")
            builddir_name, src = order_dirs(pos, used)
            if src == pos[1] and builddir_name == pos[0] and pos[0] not in used and posixpath.basename(pos[0]) not in ("build", "_build", "builddir", "output", "out"):
                builddir_name, src = pos[1], pos[0]
            style, build_dir, configure = "meson", norm(posixpath.join(cwd, src)), opts
        elif t[:2] == ["meson", "setup"] or (t[0] == "meson" and len(t) > 1 and not t[1].startswith("-") and t[1] not in ("compile", "install", "test", "configure")):
            args = t[2:] if t[1] == "setup" else t[1:]
            pos, opts = meson_args(args)
            if len(pos) == 2:
                builddir_name, src = order_dirs(pos, used)
            elif len(pos) == 1:
                builddir_name, src = pos[0], "."
            else:
                raise NotSimple("meson setup arguments")
            style, build_dir, configure, plain_meson = "meson", norm(posixpath.join(cwd, src)), opts, True
        elif t[:2] == ["meson", "compile"] or (t[0] == "ninja" and "-C" in t and "install" not in t) or t[:2] == ["cmake", "--build"]:
            if style not in ("meson", "cmake"):
                raise NotSimple("compile without setup")
        elif t[0] == "cmake" and t[1] != "--build":
            args, src, bdir, opts = t[1:], None, None, []
            i = 0
            while i < len(args):
                a = args[i]
                if a in ("-S", "-B") and i + 1 < len(args):
                    if a == "-S":
                        src = args[i + 1]
                    else:
                        bdir = args[i + 1]
                    i += 2
                    continue
                if a.startswith("-S") and len(a) > 2:
                    src = a[2:]
                elif a.startswith("-B") and len(a) > 2:
                    bdir = a[2:]
                elif a == "-G" and i + 1 < len(args):
                    i += 2
                    continue
                elif a.startswith("-G"):
                    pass
                elif a == "-D" and i + 1 < len(args):
                    opts.append("-D" + args[i + 1])
                    i += 2
                    continue
                elif a.startswith("-"):
                    opts.append(a)
                elif src is None:
                    src = a
                else:
                    raise NotSimple("cmake positional arguments")
                i += 1
            if src is None or bdir is None:
                raise NotSimple("cmake without -S and -B")
            style, build_dir, builddir_name, configure = "cmake", norm(posixpath.join(cwd, src)), bdir, opts
        elif t[0] == "./configure":
            style, build_dir, configure = "configure", cwd, t[1:]
        elif t[0] == "make":
            if "-C" in t:
                raise NotSimple("make -C in build")
            if style is None:
                style, build_dir = "make", cwd
            elif style != "configure" or cwd != build_dir:
                raise NotSimple("make after another build system")
            make_args = [a for a in t[1:] if not re.fullmatch(r"-j\d*", a)]
        else:
            raise NotSimple(f"build runs {t[0]}")
    if style is None:
        raise NotSimple("no recognised build step")
    if plain_meson and not any(o.startswith("--auto-features") for o in configure):
        configure = ["--auto-features=auto"] + configure
    installed = False
    extra = []
    for cwd, t in package.items:
        env, t = split_env(t)
        destdir = env.pop("DESTDIR", None)
        if env:
            raise NotSimple(f"environment in package: {env}")
        if t[0] == "install":
            extra.append((cwd, t))
            continue
        if style == "meson" and (t[:2] == ["meson", "install"] or (t[0] == "ninja" and "install" in t)):
            ok = is_pkgdir(destdir or "") or ("--destdir" in t and is_pkgdir(t[t.index("--destdir") + 1]))
            if not ok:
                raise NotSimple("meson install without destdir")
            installed = True
        elif style == "cmake" and (t[:2] == ["cmake", "--install"] or (t[0] in ("make", "ninja") and "-C" in t and "install" in t)):
            if not is_pkgdir(destdir or "") and "DESTDIR=@PKGDIR@" not in t and "DESTDIR=@PKGDIR@/" not in t:
                raise NotSimple("cmake install without DESTDIR")
            installed = True
        elif style in ("configure", "make") and t[0] == "make" and "install" in t:
            if cwd != build_dir:
                raise NotSimple("make install in another directory")
            rest = [a for a in t[1:] if a != "install" and not a.startswith("DESTDIR=")]
            if not any(a.startswith("DESTDIR=@PKGDIR@") for a in t) and not is_pkgdir(destdir or ""):
                raise NotSimple("make install without DESTDIR")
            if style == "make":
                rest = [a for a in rest if a != "PREFIX=/usr"]
            install_args = rest
            installed = True
        else:
            raise NotSimple(f"package runs {t[0]}")
    if not installed:
        raise NotSimple("no install step")
    return style, build_dir, configure, make_args, install_args, patches, extra


def lua_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def lua_list(items, indent="  "):
    if not items:
        return "{}"
    one = "{ " + ", ".join(lua_str(x) for x in items) + " }"
    if len(one) <= 100:
        return one
    return "{\n" + "".join(f"{indent}  {lua_str(x)},\n" for x in items) + indent + "}"


def drop_sigs(src, sums):
    keep_src, keep_sums = [], []
    for i, s in enumerate(src):
        base = s.split("::", 1)[-1]
        if base.endswith((".sig", ".asc", ".sign")):
            continue
        keep_src.append(s)
        if sums is not None:
            keep_sums.append(sums[i] if i < len(sums) else "SKIP")
    return keep_src, keep_sums


def convert(dirpath):
    data, arrays, funcs = dump(dirpath)
    name = data.get("pkgname", "")
    if "pkgname" in arrays:
        raise NotSimple("split package")
    if "pkgver" in funcs:
        raise NotSimple("pkgver() function")
    if data.get("install") or data.get("changelog"):
        raise NotSimple("install script")
    if any(k.startswith("source_") for k in arrays):
        raise NotSimple("per-architecture sources")
    for f in ("build", "package"):
        if f not in funcs:
            raise NotSimple(f"no {f}()")
    src = arrays.get("source", [])
    sum_field, sums = "sha256", None
    for key, lk in (("sha256sums", "sha256"), ("sha512sums", "sha512"), ("b2sums", "b2")):
        if key in arrays:
            sum_field, sums = lk, arrays[key]
            break
    if sums is None:
        weak = [k for k in ("md5sums", "sha1sums", "sha224sums", "sha384sums", "cksums") if k in arrays]
        remote = [s for s in src if "://" in s and not s.split("::", 1)[-1].startswith("git+")]
        if remote and weak:
            raise NotSimple(f"only {weak[0]}")
    src, sums = drop_sigs(src, sums)
    subst = {"pkgdir": "@PKGDIR@", "srcdir": "@SRCDIR@", "pkgname": name, "pkgver": data.get("pkgver", ""),
             "pkgrel": data.get("pkgrel", "")}
    for k, v in data.items():
        if k.startswith("_"):
            subst[k] = v
    order = sorted(subst, key=len, reverse=True)
    subst = {k: subst[k] for k in order}
    build = Func(body_lines(funcs["build"]), subst)
    package = Func(body_lines(funcs["package"]), subst)
    prepare = Func(body_lines(funcs["prepare"]), subst) if "prepare" in funcs else None
    style, build_dir, configure, make_args, install_args, patches, extra = convert_build(build, package, prepare)
    for v in configure + make_args + install_args:
        if "$" in v:
            raise NotSimple("dollar sign in arguments")
    patch_files = []
    for pcwd, p in patches:
        if pcwd != build_dir:
            raise NotSimple("patch applied outside the build directory")
        patch_files.append(p)
    installs, licenses = [], []
    for cwd, t in extra:
        for kind, rel, dest, mode in parse_install(cwd, t, build_dir, name):
            if kind == "license":
                licenses.append(rel)
            else:
                installs.append((rel, dest, mode))
    ver = data.get("pkgver", "")
    if not ver or re.search(r"[-/: ]", ver):
        raise NotSimple(f"version {ver!r}")

    def templ(s):
        if len(ver) >= 3:
            s = s.replace(ver, "${version}")
        return s

    tools, libs = [], []
    for d in arrays.get("makedepends", []):
        n = re.split(r"[<>=]", d)[0]
        (tools if n in TOOLS or n.startswith(TOOL_PREFIXES) else libs).append(d)
    out = ["return {"]

    def field(k, v):
        out.append(f"  {k} = {v},")

    field("name", lua_str(name))
    field("version", lua_str(ver))
    field("release", lua_str(data.get("pkgrel", "1")))
    if data.get("epoch"):
        field("epoch", lua_str(data["epoch"]))
    if data.get("pkgdesc"):
        field("desc", lua_str(data["pkgdesc"]))
    if data.get("url"):
        field("url", lua_str(data["url"]))
    for key, lk in (("license", "license"), ("arch", "arch"), ("groups", "groups")):
        if arrays.get(key):
            field(lk, lua_list(arrays[key]))
    if src:
        field("source", lua_list([templ(s) for s in src]))
        field(sum_field, lua_list(sums if sums else ["SKIP"] * len(src)))
    if arrays.get("noextract"):
        field("noextract", lua_list(arrays["noextract"]))
    for key, lk in (("depends", "depends"), ("optdepends", "optdepends")):
        if arrays.get(key):
            field(lk, lua_list(arrays[key]))
    if tools:
        field("makedepends", lua_list(tools))
    if libs:
        field("libdepends", lua_list(libs))
    for key in ("provides", "conflicts", "replaces", "backup", "options"):
        if arrays.get(key):
            field(key, lua_list(arrays[key]))
    overrides = {}
    for k, v in arrays.items():
        m = re.fullmatch(r"(depends|makedepends|optdepends|provides|conflicts)_([a-z0-9_]+)", k)
        if m and v:
            base = m.group(1)
            if base == "makedepends":
                base_list = libs
                key = "libdepends"
            else:
                base_list = arrays.get(base, [])
                key = base
            overrides.setdefault(m.group(2), {})[key] = base_list + v
    if overrides:
        out.append("  arch_overrides = {")
        for a, kv in sorted(overrides.items()):
            out.append(f"    {a} = {{")
            for k, v in kv.items():
                out.append(f"      {k} = {lua_list(v, '      ')},")
            out.append("    },")
        out.append("  },")
    if build_dir:
        field("build_dir", lua_str(templ(build_dir)))
    if patch_files:
        field("patches", lua_list(patch_files))
    field("style", lua_str(style))
    if configure:
        field("configure", lua_list(configure))
    if make_args:
        field("make_args", lua_list(make_args))
    if install_args:
        field("install_args", lua_list(install_args))
    if installs:
        out.append("  install = {")
        for rel, dest, mode in installs:
            m = "" if mode == "644" else f", mode = {mode}"
            out.append(f"    {{ {lua_str(rel)}, {lua_str(dest)}{m} }},")
        out.append("  },")
    if licenses:
        field("install_license", lua_list(licenses))
    out.append("}")
    return "\n".join(out) + "\n"


def main():
    top = sys.argv[1] if len(sys.argv) > 1 else "packages"
    write = "--write" in sys.argv
    done, skipped = [], {}
    for name in sorted(os.listdir(top)):
        d = os.path.join(top, name)
        if not os.path.isfile(os.path.join(d, "PKGBUILD")):
            continue
        try:
            lua = convert(d)
        except NotSimple as e:
            skipped[name] = str(e)
            continue
        except Exception as e:
            skipped[name] = f"error: {e}"
            continue
        done.append(name)
        if write:
            with open(os.path.join(d, "tiny.lua"), "w") as f:
                f.write(lua)
    for name, why in sorted(skipped.items()):
        print(f"skip {name}: {why}")
    print(f"converted {len(done)}, skipped {len(skipped)}")


main()
