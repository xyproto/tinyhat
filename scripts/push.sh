#!/bin/sh
# Install freshly built packages on a running Tiny Hat over SSH, without a new image or a reboot.
# Start the SSH server on the Tiny Hat first (from the application menu).
# Usage: scripts/push.sh user@host repo arch base
set -eu
target=$1
repo=$2
arch=$3
base=$4
top=$(dirname "$(dirname "$(readlink -f "$0")")")
names=$base
if [ -f "$top/packages/$base/.SRCINFO" ]; then
	names=$(sed -n 's/^pkgname = //p' "$top/packages/$base/.SRCINFO")
fi
files=
for name in $names; do
	# name-pkgver-pkgrel-arch.pkg.tar.*: two more dashes after the name skips dwl-extra-1.0-1 for dwl
	f=$(ls -t "$repo/$name"-*-"$arch".pkg.tar.* "$repo/$name"-*-any.pkg.tar.* 2>/dev/null |
		grep -v '\.sig$' | grep "/$name-[^-/]*-[^-/]*-[^-/]*\.pkg\.tar\.[^/]*\$" | head -n1 || true)
	[ -n "$f" ] && files="$files $f"
done
[ -n "$files" ] || {
	echo "push.sh: no $arch package for $base in $repo, build it first" >&2
	exit 1
}
echo "push.sh: sending$files to $target"
dir=/tmp/tinyhat-push
ssh "$target" "rm -rf $dir && mkdir -p $dir"
# -O: dropbear has no SFTP server, so use the old scp protocol
scp -O $files "$target:$dir/"
ssh -t "$target" "doas /usr/lib/tinyhat/pacman -U --noconfirm $dir/*.pkg.tar.*; rc=\$?; rm -rf $dir; exit \$rc"
