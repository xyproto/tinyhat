#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
mkdir -p "$top/build/log"
rc=0
for dir in "$top"/pkgbuilds/tinyhat/*/; do
	set -- "$dir"*.patch
	[ -e "$1" ] || continue
	base=$(basename "$dir")
	log=$top/build/log/patchcheck-$base.log
	if "$top/scripts/makepkg.sh" --nobuild "$dir" >"$log" 2>&1; then
		echo "$base: ok ($# patches)"
	else
		echo "$base: prepare() failed, see $log" >&2
		rc=1
	fi
done
exit $rc
