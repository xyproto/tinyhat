#!/bin/bash
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
dir=$top/packages/tinyhat-alpine
mirror=https://dl-cdn.alpinelinux.org/alpine/edge
roots="alpine-baselayout alpine-release apk-tools alpine-keys ca-certificates-bundle"
skip=" busybox busybox-binsh bash bash-binsh "

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

resolve() {
	awk -v roots="$roots" -v skip="$skip" '
	BEGIN { RS = ""; FS = "\n" }
	{
		n = v = d = p = ""
		for (i = 1; i <= NF; i++) {
			t = substr($i, 1, 2); x = substr($i, 3)
			if (t == "P:") n = x; else if (t == "V:") v = x; else if (t == "D:") d = x; else if (t == "p:") p = x
		}
		ver[n] = v; deps[n] = d
		if (!(n in prov)) prov[n] = n
		k = split(p, pv, " ")
		for (i = 1; i <= k; i++) { q = pv[i]; sub(/=.*/, "", q); if (!(q in prov)) prov[q] = n }
	}
	END {
		q = split(roots, queue, " ")
		while (h < q) {
			x = queue[++h]
			if (x ~ /^!/) continue
			sub(/[<>=~].*/, "", x)
			if (x == "/bin/sh" || x ~ /^cmd:/) continue
			if (!(x in prov)) { print "alpine-pins: no package provides " x > "/dev/stderr"; exit 1 }
			pk = prov[x]
			if (index(skip, " " pk " ") || (pk in seen)) continue
			seen[pk] = 1
			m = split(deps[pk], dd, " ")
			for (j = 1; j <= m; j++) queue[++q] = dd[j]
		}
		for (pk in seen) print pk "-" ver[pk] ".apk"
		print "apk-tools-static-" ver["apk-tools-static"] ".apk" > "/dev/stderr"
		print "release " ver["alpine-release"] > "/dev/stderr"
	}' "$1"
}

: >"$work/source"
for arch in x86 x86_64 aarch64; do
	curl -sSf "$mirror/main/$arch/APKINDEX.tar.gz" | tar -xzO APKINDEX >"$work/index-$arch"
	resolve "$work/index-$arch" 2>"$work/extra-$arch" | LC_ALL=C sort | while read -r f; do
		printf ' \\\n    "alpine-%s-%s::%s/main/%s/%s"' "$arch" "$f" "$mirror" "$arch" "$f"
	done >>"$work/source"
	echo "alpine-pins: $arch: $(resolve "$work/index-$arch" 2>/dev/null | wc -l) packages" >&2
done
static=$(head -n 1 "$work/extra-x86_64")
release=$(sed -n 's/^release //p' "$work/extra-x86")
release=${release%-r*}
{
	printf 'set source'
	cat "$work/source"
	printf ' \\\n    "apk-static-x86_64.apk::%s/main/x86_64/%s"' "$mirror" "$static"
	printf ' \\\n    tinyhat-alpine \\\n    adduser \\\n    addgroup \\\n    tinyhat-alpine.hook \\\n    tinyhat-host-fonts.conf\n'
} >"$work/block"

awk -v src="$work/block" -v rel="$release" '
/^set pkgver / { print "set pkgver " rel; next }
/^set source/ { while ((getline l < src) > 0) print l; skip = 1; next }
skip && /^set sha256sums/ { skip = 0 }
!skip { print }
' "$dir/TINYBUILD" >"$work/TINYBUILD"
cp -- "$work/TINYBUILD" "$dir/TINYBUILD"
fish "$top/packages/makep/root/usr/bin/makep" -C "$dir" -s "$top/vendor/src" --updsums
python3 "$top/scripts/srcinfo.py" "$dir"
echo "alpine-pins: Alpine edge $release"
