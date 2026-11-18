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
# [image-orbiton] packages in distro.toml ("make cross" builds them), and what they need at run time
pkgs=$(python3 scripts/distro.py packages image-orbiton)

files=$(python3 scripts/build_planner.py pkgfiles --image build/repo i686 $pkgs)
# filesystem first: it has the links (like usr/sbin -> bin) that the other packages' paths go through
files="$(printf '%s\n' $files | grep '/filesystem-[^/]*$') $(printf '%s\n' $files | grep -v '/filesystem-[^/]*$')"
rm -rf "$root"
mkdir -p "$root"
for f in $files; do
  bsdtar --no-xattrs --no-acls --no-fflags -xpf "$f" -C "$root" \
    --exclude .PKGINFO --exclude .BUILDINFO --exclude .MTREE --exclude .INSTALL --exclude .CHANGELOG \
    --exclude 'usr/lib/modules/*/vmlinuz'
done
echo "mkimg-orbiton.sh: $(echo "$files" | wc -l) packages"

# Machine configuration, and the tinyhat user that Orbiton runs as
cp -a config/etc/. "$root/etc/"
# Modes from the checkout depend on the umask of whoever cloned it (cp -a copies config/etc's own mode to /etc too)
(cd config/etc && find .) | while IFS= read -r p; do
  if [ -d "config/etc/$p" ]; then
    chmod 755 "$root/etc/$p"
  else
    chmod 644 "$root/etc/$p"
  fi
done
grep -q '^tinyhat:' "$root/etc/passwd" || echo 'tinyhat:x:1000:1000:Tiny Hat:/home/tinyhat:/usr/bin/osh' >> "$root/etc/passwd"
grep -q '^tinyhat:' "$root/etc/group" || echo 'tinyhat:x:1000:' >> "$root/etc/group"
grep -q '^tinyhat:' "$root/etc/shadow" || echo 'tinyhat::20000::::::' >> "$root/etc/shadow"
mkdir -p "$root/home/tinyhat"
# An empty machine-id, like the full image (scripts/mkrootfs.sh): systemd makes one at boot, and does not see the boot
# as a first boot, so systemd-firstboot does not stop it to ask for a timezone and a root password
: > "$root/etc/machine-id"
chown 1000:1000 "$root/home/tinyhat"
chmod 700 "$root/home/tinyhat"

# Only the firmware that the kernel modules ask for
python3 scripts/fwprune.py "$root"

rm -f "$sfs"
mksquashfs "$root" "$sfs" -comp xz -Xbcj x86 -b 1M -noappend -quiet
ls -l "$sfs"

TINYHAT_OWN=1 scripts/mkinitramfs.sh "$initrd"
TINYHAT_OWN=1 TINYHAT_BR="$top/$root" TINYHAT_DEFAULT=orbiton scripts/mkimg.sh "$out" build/vmlinuz "$initrd" "$sfs"
ls -l "$out"
