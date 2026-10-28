#!/bin/sh
set -eu
umask 022
root=$1
shift
top=$(dirname "$(dirname "$(readlink -f "$0")")")
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$top" log -1 --format=%ct 2>/dev/null || echo 315532800)}
cflags="-march=i686 -mtune=generic -Os -pipe -fno-plt -fno-asynchronous-unwind-tables"
mkdir -p "$HOME/.ccache"
exec setarch i686 bwrap \
	--bind "$root" / \
	--dev /dev --proc /proc --tmpfs /tmp \
	--bind "$HOME/.ccache" /ccache \
	--ro-bind /etc/resolv.conf /etc/resolv.conf \
	--unshare-all --uid 0 --gid 0 --hostname tinyhat-build \
	--clearenv \
	--setenv PATH /usr/lib/ccache/bin:/usr/bin \
	--setenv CCACHE_DIR /ccache \
	--setenv HOME /root \
	--setenv LANG C.UTF-8 \
	--setenv SOURCE_DATE_EPOCH "$epoch" \
	--setenv MAKEFLAGS "-j$(nproc)" \
	--setenv CFLAGS "$cflags" \
	--setenv CXXFLAGS "$cflags" \
	--setenv LDFLAGS "-Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now,--gc-sections" \
	"$@"
