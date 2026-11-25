#!/bin/sh
set -eu
top=$(dirname "$(dirname "$(readlink -f "$0")")")
cd "$top"
makep="fish packages/makep/root/usr/bin/makep"

if [ -n "${PKG-}" ]; then
	dirs=
	for p in $PKG; do
		[ -f "packages/$p/TINYBUILD" ] || { echo "up.sh: no packages/$p/TINYBUILD" >&2; exit 1; }
		dirs="$dirs packages/$p"
	done
else
	dirs=$(git status --porcelain --untracked-files=all -- packages | cut -c4- | sed 's|^.* -> ||' |
		cut -d/ -f1-2 | sort -u | while read -r d; do [ -f "$d/TINYBUILD" ] && echo "$d"; done)
fi

field() {
	sed -n "s/^set $1 //p" | head -n 1
}

for d in $dirs; do
	t=$d/TINYBUILD
	echo "up.sh: $d"
	new=$(field pkgver < "$t")
	old=$(git show "HEAD:$t" 2>/dev/null | field pkgver || true)
	if [ -n "$old" ] && [ "$new" != "$old" ]; then
		[ "$(field pkgrel < "$t")" = 1 ] || sed -i 's/^set pkgrel .*/set pkgrel 1/' "$t"
		echo "up.sh: $d: $old -> $new, pkgrel 1"
		if grep -q '^set -g _commit ' "$t"; then
			url=$(grep -o 'git+https://[^#?"]*' "$t" | head -n 1 | sed 's/^git+//')
			prefix=$(sed -n 's/^set nvchecker .*prefix=\([^ ]*\).*/\1/p' "$t" | tr -d "'")
			commit=
			for tag in "$prefix$new" "v$new" "$new"; do
				commit=$(git ls-remote "$url" "refs/tags/$tag^{}" "refs/tags/$tag" | sort -k2 | tail -n 1 | cut -f1)
				[ -z "$commit" ] || break
			done
			if [ -n "$commit" ]; then
				sed -i "s/^set -g _commit .*/set -g _commit $commit/" "$t"
				echo "up.sh: $d: _commit $commit ($tag)"
			else
				echo "up.sh: $d: no tag for $new at $url, _commit is unchanged" >&2
			fi
		fi
	fi
	mkdir -p vendor/src
	$makep -C "$d" -s "$top/vendor/src" --updsums
	python3 scripts/srcinfo.py "$d" > /dev/null
done

REFRESH=1 ${MAKE:-make} lock lock-x86_64 lock-aarch64
fish scripts/sumcheck.fish
git status --short -- packages distro.lock
