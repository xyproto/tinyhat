#!/usr/bin/env python3
import concurrent.futures
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import autoupdate
import distro

TOP = distro.TOP


def short(msg):
    if "HTTP 403" in msg or "HTTP 429" in msg:
        return "GitHub rate limit"
    msg = " ".join(msg.split())
    return msg if len(msg) <= 100 else msg[:97] + "..."


def check(tmp, names, env):
    r = subprocess.run(["pkgctl", "version", "check", "--json", *names], cwd=tmp, capture_output=True, text=True, env=env)
    try:
        return json.loads(r.stdout[r.stdout.index("["):r.stdout.rindex("]") + 1])
    except ValueError:
        if len(names) == 1:
            err = (r.stderr.replace("\x1b[?12h\x1b[?25h", "").strip().splitlines() or ["pkgctl failed"])[-1]
            return [{"pkgbase": names[0], "status": "failure", "message": err}]
        half = len(names) // 2
        return check(tmp, names[:half], env) + check(tmp, names[half:], env)


def main():
    only = set(sys.argv[1:])
    report = {"failed": []}
    entries = {b: c for b, c in autoupdate.nvchecker_entries().items()
               if (not only or b in only) and os.path.isfile(os.path.join(TOP, "packages", b, "PKGBUILD"))}
    entries = autoupdate.usable_sources(entries, report)
    if not entries:
        print("latest: no recipes with nvchecker settings", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        for base, conf in entries.items():
            d = os.path.join(tmp, base)
            os.makedirs(d)
            with open(os.path.join(TOP, "packages", base, "PKGBUILD")) as src, open(os.path.join(d, "PKGBUILD"), "w") as dst:
                dst.write(src.read())
            with open(os.path.join(d, ".nvchecker.toml"), "w") as f:
                f.write(f"[{distro.key(base)}]\n")
                f.writelines(f"{distro.key(k)} = {distro.value(v)}\n" for k, v in conf.items())
        env = dict(os.environ)
        token = os.environ.get("GITHUB_TOKEN")
        home = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
        if token and not os.path.isfile(os.path.join(home, "nvchecker", "keyfile.toml")):
            os.makedirs(os.path.join(tmp, ".config", "nvchecker"))
            with open(os.path.join(tmp, ".config", "nvchecker", "keyfile.toml"), "w") as f:
                f.write(f"[keys]\ngithub = {distro.value(token)}\n")
            env["XDG_CONFIG_HOME"] = os.path.join(tmp, ".config")
        print(f"latest: checking {len(entries)} recipes with pkgctl version check", file=sys.stderr, flush=True)
        names = sorted(entries)
        with concurrent.futures.ThreadPoolExecutor(4) as pool:
            batches = pool.map(lambda b: check(tmp, b, env), [names[i:i + 25] for i in range(0, len(names), 25)])
            results = [e for batch in batches for e in batch]
    behind = sorted((e["pkgbase"], e["local_version"], e["upstream_version"]) for e in results
                    if e.get("status") == "success" and e.get("out_of_date"))
    failed = sorted((e["pkgbase"], short(e.get("message") or "")) for e in results if e.get("status") != "success")
    failed += [(b, short(f"{what}: {err}")) for b, what, err in report["failed"]]
    width = max([len(b) for b, _, _ in behind] + [len(b) for b, _ in failed] + [7])
    if behind:
        print(f"{'recipe':<{width}}  {'local':<20}  upstream")
        for base, old, new in behind:
            print(f"{base:<{width}}  {old:<20}  {new}")
    for base, msg in failed:
        print(f"{base:<{width}}  failed: {msg}")
    print(f"{len(behind)} behind, {len(entries) - len(behind) - sum(e.get("status") != "success" for e in results)} up to date, {len(failed)} failed")
    if any(msg == "GitHub rate limit" for _, msg in failed):
        print("Set GITHUB_TOKEN, or add a github key to ~/.config/nvchecker/keyfile.toml, to avoid the GitHub rate limit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
