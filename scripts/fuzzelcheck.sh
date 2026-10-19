#!/bin/sh
set -eu
top=$(dirname "$(dirname "$(readlink -f "$0")")")
pkgfile=$(python3 "$top/scripts/pkg.py" repofiles "$top/build/repo" i686 fuzzel 2>/dev/null) || pkgfile=
if [ -z "$pkgfile" ] || [ ! -d "$top/build/buildroot/usr" ]; then
	echo "fuzzel: skipped (build fuzzel first to check the config)"
	exit 0
fi
stage=$top/build/buildroot/.fuzzelcheck
rm -rf "$stage"
mkdir -p "$stage/fuzzel" "$stage/pkg"
bsdtar -xf "$pkgfile" -C "$stage/pkg" usr/bin/fuzzel
cp "$top/config/overlay/etc/xdg/fuzzel/fuzzel.ini" "$stage/fuzzel/fuzzel.ini"
out=$(setarch i686 bwrap --bind "$top/build/buildroot" / --dev /dev --proc /proc --tmpfs /tmp \
	--unshare-all --uid 0 --gid 0 --clearenv \
	--setenv PATH /usr/bin --setenv HOME /root --setenv LANG C.UTF-8 \
	--setenv XDG_CONFIG_HOME /.fuzzelcheck \
	/.fuzzelcheck/pkg/usr/bin/fuzzel --dmenu </dev/null 2>&1) || true
rm -rf "$stage"
case $out in
*"failed to connect to wayland"*) ;;
*)
	echo "fuzzel: does not start with the shipped config:" >&2
	printf '%s\n' "$out" >&2
	exit 1
	;;
esac
if printf '%s\n' "$out" | grep "err:" | grep -vq "failed to connect to wayland"; then
	echo "fuzzel: rejects the shipped config:" >&2
	printf '%s\n' "$out" >&2
	exit 1
fi
echo "fuzzel: ok (the shipped config loads)"
