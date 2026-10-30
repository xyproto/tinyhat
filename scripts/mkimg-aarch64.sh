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
br=$top/build/buildroot
esp_mib=16
efi_modules="fat part_msdos part_gpt xfs search search_label normal linux echo test sleep reboot halt configfile efi_gop font gfxterm gfxmenu png loadenv smbios regexp"
pkg() { python3 "$top/scripts/pkg.py" repofile "$top/build/repo-$1" "$1" "$2"; }
host() { python3 "$top/scripts/pkg.py" repofile "$top/build/repo" i686 "$1"; }
rm -rf "$work"
mkdir -p "$work/root/boot/grub" "$work/root/tinyhat" "$work/kernel" "$work/grub" "$work/grubarm" "$work/kbd"
bsdtar -xf "$(pkg aarch64 linux-aarch64)" -C "$work/kernel" boot/Image
cp "$work/kernel/boot/Image" "$work/root/boot/vmlinuz"
cp "$initrd" "$work/root/boot/initramfs.img"
bsdtar -xf "$(host grub)" -C "$work/grub" usr/bin/grub-mkimage usr/bin/grub-mkfont
mkdir -p "$work/grubarm/usr/lib/grub"
cp -a "$top/build/buildroot-aarch64/usr/lib/grub/arm64-efi" "$work/grubarm/usr/lib/grub/"
bsdtar -xf "$(host kbd)" -C "$work/kbd" usr/share/kbd/consolefonts/default8x16.psfu.gz
cp "$top/config/grub.cfg" "$work/root/boot/grub/grub.cfg"
echo "set tinyhat_arch=aarch64" >"$work/root/boot/grub/arch.cfg"
cp "$sfs" "$work/root/tinyhat/root.sfs"
cp "$top/config/grub-early.cfg" "$work/early.cfg"
theme=$work/root/boot/grub/themes/tinyhat
python3 "$top/scripts/mkgrubtheme.py" "$theme" "$work/kbd/usr/share/kbd/consolefonts/default8x16.psfu.gz"
for size in 16 32; do
	"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
		/img/grub/usr/bin/grub-mkfont -s "$size" -o "/img/root/boot/grub/themes/tinyhat/vga-$size.pf2" "/img/root/boot/grub/themes/tinyhat/vga-$size.bdf"
	rm -f "$theme/vga-$size.bdf"
done
mkdir -p "$work/efi/EFI/BOOT" "$work/efi/tinyhat"
{
	echo "# GRUB Environment Block"
	head -c $((1024 - 25)) /dev/zero | tr '\0' '#'
} >"$work/efi/tinyhat/grubenv"
"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
	/img/grub/usr/bin/grub-mkimage -d /img/grubarm/usr/lib/grub/arm64-efi -O arm64-efi -o /img/efi/EFI/BOOT/BOOTAA64.EFI -c /img/early.cfg -p /boot/grub $efi_modules
rm -rf "$work/kernel" "$work/grub" "$work/grubarm" "$work/kbd"
find "$work" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
truncate -s "${esp_mib}M" "$work/esp.img"
"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
	/bin/sh -ec 'mkfs.fat --invariant -F 16 /img/esp.img >/dev/null; mcopy -s -i /img/esp.img /img/efi/EFI ::/EFI; mcopy -s -i /img/esp.img /img/efi/tinyhat ::/tinyhat'
rm -f "$out.tmp"
"$top/scripts/mkdisk.sh" "$out.tmp" "$work/root" "$work/esp.img" ef "$esp_mib"
mv "$out.tmp" "$out"
rm -f "$work/esp.img"
rm -rf "$work/efi"
