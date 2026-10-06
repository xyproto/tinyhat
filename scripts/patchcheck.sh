#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
rc=0
for dir in "$top"/vendor/patches/*/; do
	pkg=${dir%/}
	pkg=${pkg##*/}
	recipe=$top/recipes/$pkg.sh
	if [ ! -f "$recipe" ]; then
		echo "$pkg: no recipes/$pkg.sh" >&2
		rc=1
		continue
	fi
	src=$(awk '/tar .*-xf \/src\// && !/ -C / { sub(/.*\/src\//, ""); sub(/[ "].*/, ""); print; exit }' "$recipe")
	if [ -z "$src" ]; then
		echo "$pkg: no source tarball in recipes/$pkg.sh" >&2
		rc=1
		continue
	fi
	if [ ! -f "$top/vendor/src/$src" ]; then
		echo "$pkg: missing vendor/src/$src (run make sources)" >&2
		rc=1
		continue
	fi
	work=$tmp/$pkg
	mkdir -p "$work"
	tar --no-same-owner -xf "$top/vendor/src/$src" -C "$work" --strip-components=1
	patches=$(sed -n "s|.*-i /patches/$pkg/\([^ \"']*\)\.patch.*|\1.patch|p" "$recipe")
	if [ -z "$patches" ]; then
		patches=$(cd "$dir" && printf '%s\n' *.patch | sort)
	fi
	n=0
	ok=1
	for p in $patches; do
		if [ ! -f "$dir$p" ]; then
			echo "$pkg: no such patch: $p" >&2
			rc=1
			ok=0
			break
		fi
		if ! patch -Np1 -d "$work" -s -i "$dir$p"; then
			echo "$pkg: $p does not apply cleanly to $src" >&2
			rc=1
			ok=0
			break
		fi
		n=$((n + 1))
	done
	rm -rf "$work"
	if [ "$ok" = 1 ]; then
		echo "$pkg: ok ($n patches against $src)"
	fi
done
exit $rc
