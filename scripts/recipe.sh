#!/bin/sh
set -eu
umask 022
root=$1
name=$2
top=$(dirname "$(dirname "$(readlink -f "$0")")")
mkdir -p "$root/src" "$root/work" "$root/out"
exec "$top/scripts/chroot.sh" "$root" \
	--ro-bind "$top/vendor/src" /src \
	--ro-bind "$top/recipes" /recipes \
	--ro-bind "$top/programs" /programs \
	--ro-bind "$top/vendor/patches" /patches \
	/bin/sh "/recipes/$name.sh"
