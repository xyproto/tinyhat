#!/bin/sh
# Builds build/buildroot, the i686 build environment, from our own i686 packages: [buildroot] i686 in distro.toml and
# what those need at run time, as "make cross" built them. Nothing in it comes from archlinux32.
#
# Perl is banned from Tiny Hat, so what only runs at build time (perl, autoconf, automake, help2man, intltool) and the
# tools that fetch and install (git, curl, pacman, fish for makep) are the x86_64 build environment's: it is at
# /host in every build (scripts/buildpackage.sh), [buildroot] host_tools are linked in /usr/bin and host_dirs at their
# own paths. x86_64 programs run as on a multilib system: /lib64 has their loader, and ld.so.cache has /host/usr/lib
# too (i686 programs skip those 64-bit libraries).
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
cd "$top"
root=build/buildroot
host=build/buildroot-x86_64

names=$(python3 scripts/distro.py packages buildroot i686)
# Without the package directories of [buildroot] provided (like oils-for-unix, which provides sh): the build
# environment has those from /host
without=$(python3 scripts/build_planner.py query provided-bases | paste -sd,)
files=$(python3 scripts/build_planner.py pkgfiles --image --without "$without" build/repo i686 $names)
# filesystem first: it has the links (like usr/sbin -> bin) that the other packages' paths go through
files="$(printf '%s\n' $files | grep '/filesystem-[^/]*$') $(printf '%s\n' $files | grep -v '/filesystem-[^/]*$')"

chmod -R u+rwX "$root" 2> /dev/null || true
rm -rf "$root"
mkdir -p "$root"
lock=build/buildroot.files
: > "$lock"
for f in $files; do
  bsdtar --no-xattrs --no-acls --no-fflags --no-same-owner -xpf "$f" -C "$root" \
    --exclude .PKGINFO --exclude .BUILDINFO --exclude .MTREE --exclude .INSTALL --exclude .CHANGELOG
  # NAME VERSION FILE, for the post_install functions (scripts/postinstall.sh reads "repo name version file sha256")
  info=$(bsdtar -xOf "$f" .PKGINFO)
  n=$(printf '%s\n' "$info" | sed -n 's/^pkgname = //p')
  v=$(printf '%s\n' "$info" | sed -n 's/^pkgver = //p')
  echo "tinyhat $n $v ${f##*/} -" >> "$lock"
done
echo "mkbuildroot.sh: $(echo "$files" | wc -w) packages"
# pacman's database of what is installed, so that the builds install only what they add (pacman -U)
python3 scripts/mklocaldb.py "$lock" build/repo "$root" buildroot

for t in $(python3 scripts/distro.py packages buildroot host_tools); do
  [ -e "$host/usr/bin/$t" ] || { echo "mkbuildroot.sh: $host has no usr/bin/$t" >&2; exit 1; }
  [ ! -e "$root/usr/bin/$t" ] || { echo "mkbuildroot.sh: usr/bin/$t is in our packages too" >&2; exit 1; }
  ln -s "/host/usr/bin/$t" "$root/usr/bin/$t"
done
for t in $(python3 scripts/distro.py packages buildroot host_python_tools); do
  [ -e "$host/usr/bin/$t" ] || { echo "mkbuildroot.sh: $host has no usr/bin/$t" >&2; exit 1; }
  printf '#!/bin/sh\n# The x86_64 build environment'"'"'s %s, with its own Python (scripts/mkbuildroot.sh)\nPYTHONHOME=/host/usr exec /host/usr/bin/python3 /host/usr/bin/%s "$@"\n' "$t" "$t" > "$root/usr/bin/$t"
  chmod 755 "$root/usr/bin/$t"
done
for d in $(python3 scripts/distro.py packages buildroot host_dirs); do
  [ -e "$host/$d" ] || { echo "mkbuildroot.sh: $host has no $d" >&2; exit 1; }
  rm -rf "${root:?}/$d"
  mkdir -p "$(dirname "$root/$d")"
  ln -s "/host/$d" "$root/$d"
done
# No Info manuals: makeinfo (and texi2any) for what looks for the program (CMake's find_program) rather than MAKEINFO
for t in makeinfo texi2any; do
  install -m755 scripts/makeinfo-stub "$root/usr/bin/$t"
done
# The loader of x86_64 programs, and their libraries in ld.so.cache
rm -f "$root/lib64" "$root/usr/lib64"
ln -s /host/usr/lib "$root/lib64"
mkdir -p "$root/etc/ld.so.conf.d"
echo /host/usr/lib > "$root/etc/ld.so.conf.d/host.conf"
# pacman (the x86_64 one) installs the build dependencies, without repositories
printf '[options]\nArchitecture = i686\nSigLevel = Never\n' > "$root/etc/pacman.conf"

bwrap --bind "$root" / --ro-bind "$host" /host --dev /dev --proc /proc --tmpfs /tmp --unshare-all --clearenv \
  --setenv PATH /usr/bin /usr/bin/ldconfig
HOSTENV=$host scripts/postinstall.sh "$root" "$lock" build/repo
