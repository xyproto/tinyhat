#!/bin/sh
set -eu
root=$(readlink -f "$1")
lock=$2
pkgdir=$3

inroot() {
	bwrap --bind "$root" / --dev /dev --proc /proc --tmpfs /tmp --unshare-all --clearenv \
		--setenv PATH /usr/bin --setenv LANG C.UTF-8 "$@"
}

mkdir -p "$root/var/lib/tinyhat-install"
while read -r _ name ver file _; do
	bsdtar -xOf "$pkgdir/$file" .INSTALL >"$root/var/lib/tinyhat-install/$name" 2>/dev/null || {
		rm -f "$root/var/lib/tinyhat-install/$name"
		continue
	}
	inroot /bin/sh -c '. "$1"; if command -v post_install >/dev/null; then post_install "$2"; fi' \
		sh "/var/lib/tinyhat-install/$name" "$ver" </dev/null >/dev/null 2>&1 || echo "postinstall.sh: $name post_install failed" >&2
done <"$lock"
rm -rf "$root/var/lib/tinyhat-install"

inroot /usr/bin/update-ca-trust
[ ! -x "$root/usr/bin/gdk-pixbuf-query-loaders" ] || [ ! -d "$root/usr/lib/gdk-pixbuf-2.0/2.10.0" ] || inroot /usr/bin/gdk-pixbuf-query-loaders --update-cache
[ ! -x "$root/usr/bin/glib-compile-schemas" ] || inroot /usr/bin/glib-compile-schemas /usr/share/glib-2.0/schemas
[ ! -x "$root/usr/bin/gio-querymodules" ] || inroot /usr/bin/gio-querymodules /usr/lib/gio/modules
[ ! -x "$root/usr/bin/fc-cache" ] || inroot /usr/bin/fc-cache -s
[ ! -x "$root/usr/bin/gtk-query-immodules-3.0" ] || inroot /usr/bin/gtk-query-immodules-3.0 --update-cache
