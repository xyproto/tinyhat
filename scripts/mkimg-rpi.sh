#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
: "${SOURCE_DATE_EPOCH:=$(git -C "$top" log -1 --format=%ct 2>/dev/null || echo 315532800)}"
export SOURCE_DATE_EPOCH
out=$1
initrd=$2
sfs=$3
work=$top/build/img/$(basename "$out" .img)
boot_mib=128
pkg() { python3 "$top/scripts/pkg.py" repofile "$top/build/repo-aarch64" aarch64 "$1"; }
rm -rf "$work"
mkdir -p "$work/root/tinyhat"
bsdtar -xf "$(pkg raspberrypi-bootloader)" -C "$work" boot
bsdtar -xf "$(pkg linux-rpi)" -C "$work" --exclude boot/config.txt --exclude boot/cmdline.txt boot
cp "$top/config/rpi/config.txt" "$top/config/rpi/cmdline.txt" "$work/boot/"
cp "$initrd" "$work/boot/initramfs.img"
cp "$sfs" "$work/root/tinyhat/root.sfs"
find "$work" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
truncate -s "${boot_mib}M" "$work/boot.img"
"$top/scripts/chroot.sh" "$top/build/buildroot" --bind "$work" /img \
	/bin/sh -ec 'mkfs.fat --invariant -F 32 -n TINYHATBOOT /img/boot.img >/dev/null; mcopy -s -m -i /img/boot.img /img/boot/* ::/'
rm -f "$out.tmp"
"$top/scripts/mkdisk.sh" "$out.tmp" "$work/root" "$work/boot.img" c "$boot_mib"
mv "$out.tmp" "$out"
rm -rf "$work"
