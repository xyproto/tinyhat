#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
nobuild=
arch=i686
bootstrap=
cross=
without=
name=
stamp=
env=
while :; do
  case ${1-} in
    --nobuild)
      nobuild=--nobuild
      shift
      ;;
    --arch)
      arch=$2
      shift 2
      ;;
    --bootstrap)
      bootstrap=1
      shift
      ;;
    --cross)
      # i686 packages built in the x86_64 build environment with the i686-cross toolchain, so that no
      # prebuilt i686 package is needed: i686/NAME dependencies go into the toolchain's sysroot
      cross=1
      shift
      ;;
    --name)
      # The build's name, like cross-glibc: its output goes to build/log/NAME.log, and build/state/NAME tells how it
      # went (state=running, built, up-to-date, fetched, failed or stopped; start, end, log, reason)
      name=$2
      shift 2
      ;;
    --stamp)
      # Written with the cache entry (the key and the package files) after a build or a cache hit, only when it
      # changed, so that ninja (restat) runs what depends on this build only then
      stamp=$2
      shift 2
      ;;
    --env)
      # NAME=VERSION ... of what the build uses from the build environment (from scripts/build_planner.py): part of the key
      env=$2
      shift 2
      ;;
    --without)
      # --cross: leave out the packages of these package directories (BASE,BASE...), for the first build of a
      # package in a dependency cycle, before the rest of the cycle is built
      without=$2
      shift 2
      ;;
    *) break ;;
  esac
done
dir=$(readlink -f "$1")
shift
started=$(date +%s)
result=failed
setstate() {
  [ -n "$name" ] || return 0
  {
    echo "state=$1"
    echo "start=$started"
    [ "$1" = running ] || echo "end=$(date +%s)"
    echo "log=build/log/$name.log"
    # The last line of the log that is not empty, usually the error
    [ "$1" != failed ] || echo "reason=$(grep -v '^[[:space:]]*$' "$top/build/log/$name.log" | tail -n 1 | cut -c1-200)"
  } > "$top/build/state/$name.tmp"
  mv -f "$top/build/state/$name.tmp" "$top/build/state/$name"
}
if [ -n "$name" ]; then
  mkdir -p "$top/build/log" "$top/build/state"
  exec > "$top/build/log/$name.log" 2>&1
  setstate running
fi
# Removes the build tree (unless KEEP_WORK=1 keeps it, to look into a failure) and records how the build went
finish() {
  rc=$1
  if [ -z "${KEEP_WORK-}" ] && [ -n "${ownwork-}" ]; then
    chmod -R u+rwX "$work" 2> /dev/null || true
    rm -rf "$work"
  fi
  if [ "$rc" = 0 ] && [ -n "$stamp" ] && [ -f "${cache-}" ]; then
    mkdir -p "$(dirname "$stamp")"
    cp "$cache" "$stamp.new"
    if cmp -s "$stamp.new" "$stamp"; then rm -f "$stamp.new"; else mv -f "$stamp.new" "$stamp"; fi
  fi
  case $rc in
    0) setstate "$result" ;;
    129 | 130 | 143) setstate stopped ;;
    *) setstate failed ;;
  esac
}
trap 'finish $?' EXIT
trap 'exit 143' TERM INT HUP
base=${dir##*/}
[ -f "$dir/TINYBUILD" ] || { echo "$0: no TINYBUILD in $dir" >&2; exit 1; }
themeenv=$(python3 "$top/scripts/distro.py" theme-env "$base")
seed=$top/build/buildroot
repo=$top/build/repo
boot=$top/build/bootstrap
dest=$repo
work=$top/build/chroot/$arch-$base
[ -z "$cross" ] || work=$top/build/chroot/cross-$base
[ -z "$cross" ] || [ "$arch" = i686 ] || work=$top/build/chroot/cross-$arch-$base
if [ -n "$bootstrap" ]; then
  dest=$boot
  work=$top/build/chroot/boot-$base
fi
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$top" log -1 --format=%ct 2> /dev/null || echo 315532800)}
tc=x86_64-pc-linux-gnux32
path=/usr/bin:/usr/bin/site_perl:/usr/bin/vendor_perl:/usr/bin/core_perl
libpath=
personality=i686
ignorearch=
rustcross=
hostloader=
crosscc=
case $arch in
  x32)
    path=/usr/$tc/bin:/usr/bin:/usr/bin/site_perl:/usr/bin/vendor_perl:/usr/bin/core_perl
    libpath=/usr/lib32
    personality=x86_64
    ;;
  aarch64)
    seed=$top/build/buildroot-aarch64
    personality=
    ignorearch=--ignorearch
    case $(uname -m) in x86_64) rustcross=1 hostloader=1 path=/opt/tinyhat-rust/bin:$path ;; esac
    if [ -n "$rustcross" ] && command -v aarch64-linux-gnu-gcc > /dev/null; then
      crosscc=1
      path=/opt/tinyhat-cc/bin:$path
    fi
    ;;
  x86_64)
    seed=$top/build/buildroot-x86_64
    personality=
    ignorearch=--ignorearch
    ;;
esac
if [ -n "$cross" ]; then
  seed=$top/build/buildroot-x86_64
  case $arch in
    i686)
      tc=i686-tinyhat-linux-gnu
      path=/usr/$tc/bin:/usr/bin:/usr/bin/site_perl:/usr/bin/vendor_perl:/usr/bin/core_perl
      # i686 programs that are built and run during the build find their libraries in the sysroot; 64-bit
      # programs skip those libraries, since they are 32-bit
      libpath=/usr/$tc/sysroot/usr/lib
      personality=i686
      ignorearch=
      ;;
    aarch64)
      tc=aarch64-unknown-linux-gnu
      path=/opt/tinyhat-rust/bin:/usr/bin:/usr/bin/site_perl:/usr/bin/vendor_perl:/usr/bin/core_perl
      libpath=
      personality=
      ignorearch=--ignorearch
      crosscc=
      rustcross=1
      hostloader=
      ;;
    *)
      echo "buildpackage.sh: --cross is for i686 and aarch64" >&2
      exit 1
      ;;
  esac
fi
case $(uname -m) in x86_64 | i?86) ;; *) personality= ;; esac
rootarch=$arch
[ -z "$cross" ] || rootarch=x86_64
# Cross builds: glibc's iconv works on the target, but configure's test program, run on the build machine, would load
# the build machine's 64-bit gconv modules and fail (am_cv_func_iconv_works, gnulib's iconv.m4); without it, gettext's
# msgfmt cannot convert from ISO-8859-1
# Builds in the i686 build environment (i686 and x32) run what only runs at build time (perl, autoconf, git, pacman,
# fish...) from the x86_64 build environment at /host (scripts/mkbuildroot.sh)
hostenv=
[ "$seed" != "$top/build/buildroot" ] || hostenv=$top/build/buildroot-x86_64
mkdir -p "$repo" "$boot" "$dest/.cache" "$top/vendor/src" "$HOME/.ccache"

lookup() {
  kind=$1
  shift
  [ $# -gt 0 ] || return 0
  files=${files:-repofiles}
  case $kind in
    i686) out=$(python3 "$top/scripts/pkg.py" $files "$repo" i686 "$@") ;;
    # Our own builds of the packages and what they need at run time, never prebuilt ones, and never an older build
    # of this package directory's own packages
    cross) out=$(python3 "$top/scripts/build_planner.py" pkgfiles --without "$base${without:+,$without}" "$repo" "$arch" "$@") ;;
    x32 | sysroot) out=$(python3 "$top/scripts/pkg.py" $files "$repo" x32 "$@") ;;
    aarch64) out=$(python3 "$top/scripts/pkg.py" $files "$repo" aarch64 "$@") ;;
    x86_64) out=$(python3 "$top/scripts/pkg.py" $files "$repo" x86_64 "$@") ;;
    boot) out=$(python3 "$top/scripts/pkg.py" $files "$boot" x32 "$@") ;;
  esac
  printf '%s\n' "$out" | sed "s|^$repo/|/repo/|; s|^$boot/|/bootstrap/|"
}
want_i686= want_x32= want_boot= want_sysroot= want_aarch64= want_x86_64=
for d in "$@"; do
  case $d in
    i686/*) want_i686="$want_i686 ${d#*/}" ;;
    x32/*) want_x32="$want_x32 ${d#*/}" ;;
    boot/*) want_boot="$want_boot ${d#*/}" ;;
    sysroot/*) want_sysroot="$want_sysroot ${d#*/}" ;;
    aarch64/*) want_aarch64="$want_aarch64 ${d#*/}" ;;
    x86_64/*) want_x86_64="$want_x86_64 ${d#*/}" ;;
    *)
      echo "buildpackage.sh: bad dependency $d" >&2
      exit 1
      ;;
  esac
done
crossdeps=
if [ -n "$cross" ]; then
  crossdeps=$(lookup cross $want_i686 $want_aarch64)
  want_i686= want_aarch64=
fi
i686deps=$(
  lookup i686 $want_i686
  lookup aarch64 $want_aarch64
  lookup x86_64 $want_x86_64
)
x32deps="$(lookup x32 $want_x32) $(lookup boot $want_boot)"
sysdeps=$(lookup sysroot $want_sysroot)
keydeps=$(
  files=keyfiles
  lookup i686 $want_i686
  lookup aarch64 $want_aarch64
  lookup x86_64 $want_x86_64
  lookup x32 $want_x32
  lookup boot $want_boot
  lookup sysroot $want_sysroot
)

# What the build depends on, and so what its cache key is made of: the build options, the theme, the files in the
# package directory (the TINYBUILD without its nvchecker settings), the makep configuration, and the packages it is built with
inputs=$({
  echo "$arch $bootstrap $nobuild${cross:+ cross}"
  for t in $env; do echo "tool $t"; done
  printf '%s\n' "$themeenv"
  (cd "$dir" && find -L . -type f ! -name .SRCINFO | LC_ALL=C sort | while IFS= read -r f; do
    if [ "$f" = ./TINYBUILD ]; then
      printf '%s  %s\n' "$(grep -v '^set nvchecker ' "$f" | sha256sum | cut -d' ' -f1)" "$f"
    else
      sha256sum "$f"
    fi
  done)
  env -i PATH="$PATH" MAKEP_CONF="$top/packages/makep/root/etc/makep.conf" fish "$top/packages/makep/root/usr/bin/makep" --print-config -a "$arch"
  for f in $keydeps $crossdeps; do echo "${f##*/}"; done
})
key=$(printf '%s\n' "$inputs" | sha256sum | cut -d' ' -f1)
cache=$dest/.cache/$arch-$base
recorded() {
  [ -f "$cache.inputs" ] || return 1
  [ "$(printf '%s\n' "$inputs" | grep -v '\.pkg\.tar\.zst$')" = "$(grep -v '\.pkg\.tar\.zst$' "$cache.inputs")" ] || return 1
  ! printf '%s\n' "$inputs" | grep '\.pkg\.tar\.zst$' | grep -qvxF -f "$cache.inputs"
}
if [ -z "$nobuild" ] && [ -f "$cache" ] && { [ "$(head -n1 "$cache")" = "$key" ] || recorded; }; then
  hit=1
  for f in $(tail -n +2 "$cache"); do
    [ -f "$dest/$f" ] || hit=
  done
  if [ -n "$hit" ]; then
    if [ "$(head -n1 "$cache")" != "$key" ]; then
      { echo "$key"; tail -n +2 "$cache"; } > "$cache.tmp"
      printf '%s\n' "$inputs" > "$cache.inputs"
      mv "$cache.tmp" "$cache"
    fi
    echo "$base ($arch): up to date"
    result=up-to-date
    exit 0
  fi
fi

# Why it builds: what changed since the inputs of the last build (kept next to its cache key)
if [ -z "$nobuild" ]; then
  if [ -f "$cache.inputs" ]; then
    echo "$base ($arch): building, since these inputs changed (- before, + now):"
    printf '%s\n' "$inputs" | diff "$cache.inputs" - | grep '^[<>]' | sed 's/^</  -/; s/^>/  +/' || true
  elif [ -f "$cache" ]; then
    echo "$base ($arch): building (the last build did not record its inputs)"
  else
    echo "$base ($arch): building for the first time"
  fi
fi

# How many builds run at once, and one heavy build at a time, is up to ninja (pools in build/pools.ninja). A heavy
# build gets all cores, without the load limit, and runs before the others
load=${PKG_LOAD-}
prio=10
if [ "$(python3 "$top/scripts/distro.py" heavy "$base")" = 1 ]; then
  load=
  prio=0
fi

[ -z "$rustcross" ] || "$top/scripts/rust-cross.sh"
if [ -n "$crosscc" ] && { [ ! -x "$top/build/cross-cc/launch" ] || [ "$top/scripts/cross-launch.c" -nt "$top/build/cross-cc/launch" ]; }; then
  mkdir -p "$top/build/cross-cc"
  cc -O2 -static -o "$top/build/cross-cc/launch.$$" "$top/scripts/cross-launch.c"
  mv -f "$top/build/cross-cc/launch.$$" "$top/build/cross-cc/launch"
fi

chmod -R u+rwX "$work" 2> /dev/null || true
rm -rf "$work"
ownwork=1
mkdir -p "$work/upper" "$work/ovl" "$work/pkg"
cp -rL "$dir" "$work/pkg/$base"

jobs=${PKG_MAKE_JOBS:-$(nproc)}

sandbox() {
  tries=0
  while :; do
    if bwrap_run "$@" 9>&2 2> "$work/bwrap.err"; then
      return 0
    else
      rc=$?
    fi
    if [ $tries -lt 170 ] && grep -q "^bwrap: Can't make overlay mount" "$work/bwrap.err"; then
      tries=$((tries + 1))
      if [ $tries -le 50 ]; then sleep 0.1; else sleep 1; fi
      continue
    fi
    cat "$work/bwrap.err" >&2
    return $rc
  done
}

bwrap_run() {
  nice -n "$prio" ${personality:+setarch "$personality"} bwrap \
    --overlay-src "$seed" --overlay "$work/upper" "$work/ovl" / \
    --ro-bind "$top/vendor" /vendor \
    ${hostenv:+--ro-bind "$hostenv" /host} \
    --dev /dev --proc /proc --tmpfs /tmp \
    --ro-bind /etc/resolv.conf /etc/resolv.conf \
    --unshare-all --hostname tinyhat-build --clearenv \
    --setenv PATH "$path" \
    ${libpath:+--setenv LD_LIBRARY_PATH $libpath} \
    --setenv LANG C.UTF-8 \
    --setenv HWLOC_HIDE_ERRORS 2 \
    --setenv MAKEINFO /tinyhat/scripts/makeinfo-stub \
    --setenv CMAKE_POLICY_VERSION_MINIMUM 3.5 \
    --setenv SOURCE_DATE_EPOCH "$epoch" \
    --setenv TINYHAT_ARCH "$arch" \
    --setenv TINYHAT_BOOTSTRAP "$bootstrap" \
    ${cross:+--setenv am_cv_func_iconv_works yes --setenv SYSROOT /usr/$tc/sysroot} \
    ${cross:+--setenv PKG_CONFIG_SYSROOT_DIR /usr/$tc/sysroot --setenv PKG_CONFIG_FDO_SYSROOT_RULES 1 --setenv TINYHAT_SYSROOT /usr/$tc/sysroot --setenv TINYHAT_CROSS $tc --setenv PKG_CONFIG_LIBDIR /usr/$tc/sysroot/usr/lib/pkgconfig:/usr/$tc/sysroot/usr/share/pkgconfig} \
    ${rustcross:+--ro-bind /usr /host/usr} \
    ${hostloader:+--ro-bind /usr/lib/ld-linux-x86-64.so.2 /lib64/ld-linux-x86-64.so.2} \
    ${rustcross:+--ro-bind $top/build/rust-cross /opt/tinyhat-rust/toolchain --ro-bind $top/scripts/rust-cross /opt/tinyhat-rust/bin} \
    ${crosscc:+--ro-bind $top/scripts/cross-cc /opt/tinyhat-cc/bin --ro-bind $top/build/cross-cc/launch /opt/tinyhat-cc/launch} \
    ${crosscc:+--ro-bind /usr/lib/go /usr/lib/go --setenv GOARCH arm64} \
    "$@"
}

sandbox --uid 0 --gid 0 --ro-bind "$repo" /repo --ro-bind "$boot" /bootstrap \
  /bin/sh -euc '
exec 2>&9 9>&-
arch=$1 tc=$2 i686deps=$3 x32deps=$4 sysdeps=$5 crossdeps=$6
meta="--exclude .PKGINFO --exclude .BUILDINFO --exclude .MTREE --exclude .INSTALL --exclude .CHANGELOG"
mkdir -p /var/lib/pacman/local /build /startdir /srcdest /tinyhat /ccache /repo
[ -f /var/lib/pacman/local/ALPM_DB_VERSION ] || echo 9 >/var/lib/pacman/local/ALPM_DB_VERSION
grep -q "^builder:" /etc/passwd || echo "builder:x:1000:1000:builder:/build:/bin/sh" >>/etc/passwd
grep -q "^builder:" /etc/group || echo "builder:x:1000:" >>/etc/group
pacargs="--arch i686"
[ "$arch" != aarch64 ] || pacargs="--arch aarch64 --disable-sandbox"
[ "$arch" != x86_64 ] || pacargs="--arch x86_64 --disable-sandbox"
if [ -n "$i686deps" ]; then
  pacman -Udd --needed $pacargs --noconfirm --noscriptlet --overwrite "*" $i686deps
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
if [ "$tc" = i686-tinyhat-linux-gnu ] || [ "$tc" = aarch64-unknown-linux-gnu ]; then
  s=/usr/$tc/sysroot
  for f in $crossdeps; do
    # Owners do not matter in a sysroot, and the sandbox has only one group to give files
    bsdtar --no-xattrs --no-acls --no-fflags --no-same-owner -xpf "$f" -C "$s" $meta
  done
  find "$s" -type l -lname "/*" | while IFS= read -r l; do
    ln -sfn "$(realpath -m --relative-to="$(dirname "$l")" "$s$(readlink "$l")")" "$l"
  done
  [ "$tc" != i686-tinyhat-linux-gnu ] || ln -sfn "$s/usr/lib/ld-linux.so.2" /usr/lib/ld-linux.so.2
  # The cross compiler through ccache by its full name too, as the ccache package does for the native one
  if [ -x /usr/bin/ccache ] && [ -d /usr/lib/ccache/bin ]; then
    for c in gcc g++ c++; do
      ln -sfn /usr/bin/ccache "/usr/lib/ccache/bin/$tc-$c"
    done
  fi
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
' sh "$rootarch" "$tc" "$i686deps" "$x32deps" "$sysdeps" "$crossdeps"

# run [--nobuild | --packagelist]: makep on the TINYBUILD in the build environment
# Network access: for downloading the sources (run --nobuild), and while building only for TINYBUILDs with
# options network (like cargo fetch in prepare): other builds cannot download anything behind our back
buildnet=
grep -qx '	options = network' "$dir/.SRCINFO" 2> /dev/null && buildnet=--share-net
run() {
  net=$buildnet
  [ "${1-}" != --nobuild ] || net=--share-net
  set -- -C /startdir -o /repo -s /srcdest -a "$arch" "$@"
  sandbox --uid 1000 --gid 1000 $net \
    --bind "$work/pkg/$base" /startdir \
    --bind "$dest" /repo \
    --bind "$top/vendor/src" /srcdest \
    --bind "$HOME/.ccache" /ccache \
    --ro-bind "$top" /tinyhat \
    --setenv HOME /build \
    --setenv USER builder \
    --setenv CCACHE_DIR /ccache \
    --setenv CCACHE_BASEDIR /startdir \
    --setenv CCACHE_MAXSIZE "${CCACHE_MAXSIZE:-20G}" \
    --setenv TINYHAT_JOBS "$jobs" \
    --setenv TINYHAT_LOAD "$load" \
    --setenv CARGO_BUILD_JOBS "${PKG_CARGO_JOBS:-$(nproc)}" \
    --setenv TINYHAT_SOURCE_MIRROR "${TINYHAT_SOURCE_MIRROR-}" \
    --setenv TINYHAT_THEMEENV "$themeenv" \
    --setenv MAKEP_CONF /tinyhat/packages/makep/root/etc/makep.conf \
    --setenv MAKEP_FETCH /tinyhat/scripts/fetch.sh \
    --setenv MAKEFLAGS "-j$jobs${load:+ -l$load}" \
    --chdir /startdir \
    /bin/sh -c 'exec 2>&9 9>&-; eval "$TINYHAT_THEMEENV"; exec fish /tinyhat/packages/makep/root/usr/bin/makep '"$ignorearch"' "$@"' sh "$@"
}

exec 8> "$top/vendor/src/.lock"
flock 8
run --nobuild
flock -u 8
exec 8>&-
run $nobuild
[ -z "$nobuild" ] || result=fetched
if [ -z "$nobuild" ]; then
  {
    echo "$key"
    run --packagelist | sed 's|.*/||'
  } > "$cache.tmp"
  printf '%s\n' "$inputs" > "$cache.inputs"
  mv "$cache.tmp" "$cache"
  result=built
  # Remove the downloaded sources that only this package uses, unless another arch of it is building
  if [ "${KEEP_SOURCES:-0}" != 1 ] && [ -z "$(find "$top/build/chroot" -maxdepth 1 \( -name "i686-$base" -o -name "x32-$base" -o -name "aarch64-$base" -o -name "x86_64-$base" -o -name "boot-$base" -o -name "cross-$base" \) ! -path "$work")" ]; then
    exec 8> "$top/vendor/src/.lock"
    flock 8
    python3 "$top/scripts/pkg.py" prunable "$dir" | while IFS= read -r f; do
      rm -rf "$top/vendor/src/$f"
    done
    exec 8>&-
  fi
fi
