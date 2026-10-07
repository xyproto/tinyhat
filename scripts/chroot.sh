#!/bin/sh
set -eu
umask 022
root=$1
shift
top=$(dirname "$(dirname "$(readlink -f "$0")")")
mkdir -p "$top/build/ccache"
exec setarch i686 bwrap \
	--bind "$root" / \
	--dev /dev --proc /proc --tmpfs /tmp \
	--bind "$top/build/ccache" /ccache \
	--ro-bind /etc/resolv.conf /etc/resolv.conf \
	--unshare-all --uid 0 --gid 0 --hostname tinyhat-build \
	--clearenv \
	--setenv PATH /usr/lib/ccache/bin:/usr/bin \
	--setenv CCACHE_DIR /ccache \
	--setenv HOME /root \
	--setenv LANG C.UTF-8 \
	--setenv MAKEFLAGS "-j$(nproc)" \
	--setenv CFLAGS "-march=i686 -mtune=generic -Os -pipe -fno-plt -fno-asynchronous-unwind-tables" \
	--setenv CXXFLAGS "-march=i686 -mtune=generic -Os -pipe -fno-plt -fno-asynchronous-unwind-tables" \
	--setenv LDFLAGS "-Wl,-O1,--sort-common,--as-needed,-z,relro,-z,now,--gc-sections" \
	"$@"
