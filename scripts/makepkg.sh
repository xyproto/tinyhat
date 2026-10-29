#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
nobuild=
arch=i686
bootstrap=
while :; do
	case ${1-} in
	--nobuild) nobuild=-o; shift ;;
	--arch) arch=$2; shift 2 ;;
	--bootstrap) bootstrap=1; shift ;;
	*) break ;;
	esac
done
dir=$(readlink -f "$1")
shift
base=${dir##*/}
if [ "$(python3 "$top/scripts/distro.py" source "$base")" = sources ]; then
	conf=/tinyhat/config/makepkg-tinyhat.conf
else
	conf=/tinyhat/config/makepkg.conf
fi
seed=$top/build/buildroot
repo=$top/build/repo
boot=$top/build/bootstrap
dest=$repo
work=$top/build/chroot/$arch-$base
if [ -n "$bootstrap" ]; then
	dest=$boot
	work=$top/build/chroot/boot-$base
fi
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$top" log -1 --format=%ct 2>/dev/null || echo 315532800)}
tc=x86_64-pc-linux-gnux32
path=/usr/bin:/usr/bin/site_perl:/usr/bin/vendor_perl:/usr/bin/core_perl
libpath=
personality=i686
ignorearch=
case $arch in
x32)
	path=/usr/$tc/bin:/usr/bin:/usr/bin/site_perl:/usr/bin/vendor_perl:/usr/bin/core_perl
	libpath=/usr/lib32
	personality=x86_64
	;;
aarch64)
	seed=$top/build/buildroot-aarch64
	conf=${conf%.conf}-aarch64.conf
	personality=
	ignorearch=--ignorearch
	;;
esac
mkdir -p "$repo" "$boot" "$dest/.cache" "$top/vendor/src" "$HOME/.ccache"

lookup() {
	kind=$1
	shift
	[ $# -gt 0 ] || return 0
	case $kind in
	i686) out=$(python3 "$top/scripts/pkg.py" repofiles "$repo" i686 "$@") ;;
	x32 | sysroot) out=$(python3 "$top/scripts/pkg.py" repofiles "$repo" x32 "$@") ;;
	aarch64) out=$(python3 "$top/scripts/pkg.py" repofiles "$repo" aarch64 "$@") ;;
	boot) out=$(python3 "$top/scripts/pkg.py" repofiles "$boot" x32 "$@") ;;
	esac
	printf '%s\n' "$out" | sed "s|^$repo/|/repo/|; s|^$boot/|/bootstrap/|"
}
want_i686= want_x32= want_boot= want_sysroot= want_aarch64=
for d in "$@"; do
	case $d in
	i686/*) want_i686="$want_i686 ${d#*/}" ;;
	x32/*) want_x32="$want_x32 ${d#*/}" ;;
	boot/*) want_boot="$want_boot ${d#*/}" ;;
	sysroot/*) want_sysroot="$want_sysroot ${d#*/}" ;;
	aarch64/*) want_aarch64="$want_aarch64 ${d#*/}" ;;
	*) echo "makepkg.sh: bad dependency $d" >&2; exit 1 ;;
	esac
done
i686deps=$(lookup i686 $want_i686; lookup aarch64 $want_aarch64)
x32deps="$(lookup x32 $want_x32) $(lookup boot $want_boot)"
sysdeps=$(lookup sysroot $want_sysroot)

key=$({
	echo "$arch $bootstrap $nobuild"
	(cd "$dir" && find -L . -type f ! -name .SRCINFO | LC_ALL=C sort | xargs -d '\n' sha256sum)
	cat "$top/config/makepkg.conf" "$top/config/makepkg-tinyhat.conf"
	[ "$arch" != aarch64 ] || cat "$top/config/makepkg-aarch64.conf" "$top/config/makepkg-tinyhat-aarch64.conf"
	for f in $i686deps $x32deps $sysdeps; do echo "${f##*/}"; done
} | sha256sum | cut -d' ' -f1)
cache=$dest/.cache/$arch-$base
if [ -z "$nobuild" ] && [ -f "$cache" ] && [ "$(head -n1 "$cache")" = "$key" ]; then
	hit=1
	for f in $(tail -n +2 "$cache"); do
		[ -f "$dest/$f" ] || hit=
	done
	if [ -n "$hit" ]; then
		echo "$base ($arch): up to date"
		exit 0
	fi
fi

# At most PKG_JOBS packages build at once, to limit the disk space used by build trees
slot=0
if [ "${PKG_JOBS:-0}" -gt 0 ]; then
	mkdir -p "$top/build/chroot"
	while :; do
		i=1
		while [ $i -le "$PKG_JOBS" ]; do
			exec 7>"$top/build/chroot/.slot$i"
			flock -n 7 && slot=$i && break 2
			i=$((i + 1))
		done
		exec 7>&-
		sleep 2
	done
fi

distccd_pid=
cleanup() {
	[ -z "$distccd_pid" ] || kill "$distccd_pid" 2>/dev/null || true
	chmod -R u+rwX "$work" 2>/dev/null || true
	rm -rf "$work"
}
cleanup
trap cleanup EXIT
mkdir -p "$work/upper" "$work/ovl" "$work/pkg"
cp -rL "$dir" "$work/pkg/$base"

# Compiling under emulation is slow, so aarch64 builds send the compiler runs to the host's cross compiler
jobs=${PKG_MAKE_JOBS:-$(nproc)}
echo "source $conf" >"$work/makepkg.conf"
if [ "$arch" = aarch64 ] && command -v distccd >/dev/null && command -v aarch64-linux-gnu-gcc >/dev/null; then
	mkdir -p "$top/build/distcc" "$top/build/log"
	for c in cc gcc aarch64-unknown-linux-gnu-gcc; do
		ln -sfn "$(command -v aarch64-linux-gnu-gcc)" "$top/build/distcc/$c"
	done
	for c in c++ g++ aarch64-unknown-linux-gnu-g++; do
		ln -sfn "$(command -v aarch64-linux-gnu-g++)" "$top/build/distcc/$c"
	done
	port=$((36320 + slot))
	jobs=$((jobs * 2))
	DISTCCD_PATH=$top/build/distcc distccd --no-detach --daemon --listen 127.0.0.1 --port "$port" --allow 127.0.0.1 \
		--enable-tcp-insecure --jobs "$jobs" --log-file "$top/build/log/aarch64-$base.distccd.log" &
	distccd_pid=$!
	printf '%s\n' "BUILDENV=(distcc color !ccache !check !sign)" "DISTCC_HOSTS=\"127.0.0.1:$port/$jobs\"" \
		"MAKEFLAGS=\"-j$jobs\"" "NINJAFLAGS=\"-j$jobs\"" >>"$work/makepkg.conf"
fi

sandbox() {
	tries=0
	while :; do
		if bwrap_run "$@" 9>&2 2>"$work/bwrap.err"; then
			return 0
		else
			rc=$?
		fi
		if [ $tries -lt 100 ] && grep -q "^bwrap: Can't make overlay mount" "$work/bwrap.err"; then
			tries=$((tries + 1))
			sleep 0.1
			continue
		fi
		cat "$work/bwrap.err" >&2
		return $rc
	done
}

bwrap_run() {
	${personality:+setarch "$personality"} bwrap \
		--overlay-src "$seed" --overlay "$work/upper" "$work/ovl" / \
		--ro-bind "$top/vendor" /vendor \
		--dev /dev --proc /proc --tmpfs /tmp \
		--ro-bind /etc/resolv.conf /etc/resolv.conf \
		--unshare-all --hostname tinyhat-build --clearenv \
		--setenv PATH "$path" \
		${libpath:+--setenv LD_LIBRARY_PATH $libpath} \
		--setenv LANG C.UTF-8 \
		--setenv MAKEINFO true \
		--setenv SOURCE_DATE_EPOCH "$epoch" \
		--setenv TINYHAT_ARCH "$arch" \
		--setenv TINYHAT_BOOTSTRAP "$bootstrap" \
		"$@"
}

sandbox --uid 0 --gid 0 --ro-bind "$repo" /repo --ro-bind "$boot" /bootstrap \
	/bin/sh -euc '
exec 2>&9 9>&-
arch=$1 tc=$2 i686deps=$3 x32deps=$4 sysdeps=$5
meta="--exclude .PKGINFO --exclude .BUILDINFO --exclude .MTREE --exclude .INSTALL --exclude .CHANGELOG"
mkdir -p /var/lib/pacman/local /build /startdir /srcdest /tinyhat /ccache /repo
[ -f /var/lib/pacman/local/ALPM_DB_VERSION ] || echo 9 >/var/lib/pacman/local/ALPM_DB_VERSION
grep -q "^builder:" /etc/passwd || echo "builder:x:1000:1000:builder:/build:/bin/sh" >>/etc/passwd
grep -q "^builder:" /etc/group || echo "builder:x:1000:" >>/etc/group
pacargs="--arch i686"
[ "$arch" != aarch64 ] || pacargs="--arch aarch64 --disable-sandbox"
if [ -n "$i686deps" ]; then
	pacman -Udd $pacargs --noconfirm --noscriptlet --overwrite "*" $i686deps
fi
if [ -n "$sysdeps" ]; then
	s=/usr/$tc/sysroot
	mkdir -p "$s/usr/include"
	for f in $sysdeps; do
		bsdtar --no-xattrs --no-acls --no-fflags -xpf "$f" -C "$s" $meta
	done
	for d in linux asm asm-generic drm misc mtd rdma scsi sound video xen; do
		[ ! -d "/usr/include/$d" ] || cp -a "/usr/include/$d" "$s/usr/include/"
	done
	ln -sfn lib "$s/usr/libx32"
	ln -sfn usr/lib "$s/libx32"
fi
if [ "$arch" = x32 ]; then
	mkdir -p /usr/lib32
	find /usr/lib -maxdepth 1 ! -type d \( -name "*.so" -o -name "*.so.*" -o -name "*.a" -o -name "*.o" \) \
		! -name ld-linux.so.2 -exec mv -t /usr/lib32 {} +
	for d in pkgconfig cmake; do
		[ ! -d "/usr/lib/$d" ] || mv "/usr/lib/$d" "/usr/lib32/$d"
	done
	rm -f /etc/ld.so.cache
	ln -sfn lib /usr/libx32
	ln -sfn usr/lib /libx32
	for f in $x32deps; do
		n=$(bsdtar -xOf "$f" .PKGINFO | sed -n "s/^pkgname = //p")
		ex=
		if ls -d /var/lib/pacman/local/"$n"-[0-9]* >/dev/null 2>&1; then
			ex="--exclude usr/bin --exclude usr/sbin --exclude usr/lib/gconv"
		fi
		bsdtar --no-xattrs --no-acls --no-fflags -xpf "$f" -C / $meta $ex
	done
fi
' sh "$arch" "$tc" "$i686deps" "$x32deps" "$sysdeps"

run() {
	sandbox --uid 1000 --gid 1000 --share-net \
		--bind "$work/pkg/$base" /startdir \
		--bind "$dest" /repo \
		--bind "$top/vendor/src" /srcdest \
		--bind "$HOME/.ccache" /ccache \
		--ro-bind "$top" /tinyhat \
		--ro-bind "$work/makepkg.conf" /etc/makepkg-build.conf \
		--setenv HOME /build \
		--setenv USER builder \
		--setenv CCACHE_DIR /ccache \
		--setenv CCACHE_MAXSIZE "${CCACHE_MAXSIZE:-2G}" \
		--setenv TINYHAT_JOBS "$jobs" \
		--setenv CARGO_BUILD_JOBS "${PKG_MAKE_JOBS:-$(nproc)}" \
		--setenv FAKEROOTDONTTRYCHOWN 1 \
		--setenv TINYHAT_SOURCE_MIRROR "${TINYHAT_SOURCE_MIRROR-}" \
		--chdir /startdir \
		/bin/sh -c 'exec 2>&9 9>&-; exec makepkg "$@"' sh --config /etc/makepkg-build.conf -d --noconfirm --skippgpcheck --holdver $ignorearch "$@"
}

exec 8>"$top/vendor/src/.lock"
flock 8
run --verifysource -f
flock -u 8
exec 8>&-
run -f $nobuild
if [ -z "$nobuild" ]; then
	{
		echo "$key"
		run --packagelist | sed 's|.*/||'
	} >"$cache.tmp"
	mv "$cache.tmp" "$cache"
	# Remove the downloaded sources that only this package uses, unless another arch of it is building
	if [ "${KEEP_SOURCES:-0}" != 1 ] && [ -z "$(find "$top/build/chroot" -maxdepth 1 \( -name "i686-$base" -o -name "x32-$base" -o -name "aarch64-$base" -o -name "boot-$base" \) ! -path "$work")" ]; then
		exec 8>"$top/vendor/src/.lock"
		flock 8
		python3 "$top/scripts/pkg.py" prunable "$dir" | while IFS= read -r f; do
			rm -rf "$top/vendor/src/$f"
		done
		exec 8>&-
	fi
fi
cleanup
