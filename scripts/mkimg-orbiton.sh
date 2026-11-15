#!/bin/sh
# Builds build/tinyhat32.img with only what it takes to boot GRUB, Linux and Orbiton, from packages that our
# TINYBUILDs build (scripts/buildpackage.sh --cross), never prebuilt ones. Run through scripts/userns.sh, for file owners.
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
cd "$top"
: "${SOURCE_DATE_EPOCH:=$(git log -1 --format=%ct 2> /dev/null || echo 315532800)}"
export SOURCE_DATE_EPOCH
out=${1:-build/tinyhat32.img}
root=build/rootfs-orbiton
sfs=build/root-orbiton.sfs
initrd=build/initramfs-orbiton.img
# What orbiton-init and the persist script use, and their runtime dependencies come along
pkgs="filesystem glibc bash coreutils util-linux systemd kmod kbd orbiton tinyhat-system grep gawk sed xfsprogs findutils"

files=$(python3 scripts/buildplan.py pkgfiles build/repo i686 $pkgs)
rm -rf "$root"
mkdir -p "$root"
for f in $files; do
  bsdtar --no-xattrs --no-acls --no-fflags -xpf "$f" -C "$root" \
    --exclude .PKGINFO --exclude .BUILDINFO --exclude .MTREE --exclude .INSTALL --exclude .CHANGELOG
done
echo "mkimg-orbiton.sh: $(echo "$files" | wc -l) packages"

# Machine configuration, and the tinyhat user that Orbiton runs as
cp -a config/etc/. "$root/etc/"
grep -q '^tinyhat:' "$root/etc/passwd" || echo 'tinyhat:x:1000:1000:Tiny Hat:/home/tinyhat:/usr/bin/bash' >> "$root/etc/passwd"
grep -q '^tinyhat:' "$root/etc/group" || echo 'tinyhat:x:1000:' >> "$root/etc/group"
grep -q '^tinyhat:' "$root/etc/shadow" || echo 'tinyhat::20000::::::' >> "$root/etc/shadow"
mkdir -p "$root/home/tinyhat"
chown 1000:1000 "$root/home/tinyhat"
chmod 700 "$root/home/tinyhat"

# Kernel modules, and only the firmware that they ask for
cp -a build/modules/. "$root/"
python3 scripts/fwprune.py "$root"

rm -f "$sfs"
mksquashfs "$root" "$sfs" -comp xz -Xbcj x86 -b 1M -noappend -quiet -mkfs-time "$SOURCE_DATE_EPOCH" -all-time "$SOURCE_DATE_EPOCH"
ls -l "$sfs"

TINYHAT_OWN=1 scripts/mkinitramfs.sh "$initrd"
TINYHAT_OWN=1 TINYHAT_BR="$top/$root" TINYHAT_DEFAULT=orbiton scripts/mkimg.sh "$out" build/vmlinuz "$initrd" "$sfs"
ls -l "$out"
