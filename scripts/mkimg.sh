#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
out=$1
kernel=$2
initrd=$3
sfs=$4
work=$top/build/img
br=$top/build/buildroot
esp_mib=16
rm -rf "$work"
mkdir -p "$work/root/boot/grub" "$work/root/tinyhat"
cp "$kernel" "$work/root/boot/vmlinuz"
cp "$initrd" "$work/root/boot/initramfs.img"
cp "$top/config/grub.cfg" "$work/root/boot/grub/grub.cfg"
bsdtar -xf "$top/vendor/pkg/$(awk '$2 == "memtest86+" { print $4 }' "$top/initramfs.lock")" -C "$work/root" boot/memtest86+/memtest.bin
cp "$sfs" "$work/root/tinyhat/root.sfs"
cp "$top/config/grub-early.cfg" "$work/early.cfg"

"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
	grub-mkimage -O i386-pc -o /img/core.img -c /img/early.cfg -p /boot/grub \
	biosdisk part_msdos xfs search search_label normal linux linux16 chain vbe echo test sleep reboot halt configfile
mkdir -p "$work/efi/EFI/BOOT"
"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
	grub-mkimage -O i386-efi -o /img/efi/EFI/BOOT/BOOTIA32.EFI -c /img/early.cfg -p /boot/grub \
	fat part_msdos xfs search search_label normal linux echo test sleep reboot halt configfile
cp "$br/usr/lib/grub/i386-pc/boot.img" "$work/boot.img"
truncate -s "${esp_mib}M" "$work/esp.img"
"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
	/bin/sh -c 'mkfs.fat -F 16 /img/esp.img >/dev/null && mcopy -s -i /img/esp.img /img/efi/EFI ::/EFI'

content=$(du -sm --apparent-size "$work/root" | cut -f1)
part_mib=$((content + content / 32 + 12))
truncate -s "${part_mib}M" "$work/part.img"
TEST_DIR=1 TEST_DEV=1 QA_CHECK_FS=xfs mkfs.xfs -q -f -L TINYHAT \
	-m crc=1,finobt=0,rmapbt=0,reflink=0,bigtime=1,inobtcount=0,metadir=0 \
	-i nrext64=0,exchange=0 -n parent=0 -l size=16m \
	-p "$work/root" "$work/part.img"

rm -f "$out.tmp"
truncate -s "$((part_mib + esp_mib + 1))M" "$out.tmp"
printf 'label: dos\nlabel-id: 0x7417a700\nstart=2048, size=%d, type=ef\nstart=%d, type=83, bootable\n' \
	"$((esp_mib * 2048))" "$((2048 + esp_mib * 2048))" | sfdisk -q "$out.tmp"
dd if="$work/esp.img" of="$out.tmp" bs=1M seek=1 conv=notrunc status=none
dd if="$work/part.img" of="$out.tmp" bs=1M seek="$((esp_mib + 1))" conv=notrunc status=none
python3 "$top/scripts/mbr.py" "$out.tmp" "$work/boot.img" "$work/core.img"
mv "$out.tmp" "$out"
rm -f "$work/part.img" "$work/esp.img"
rm -rf "$work/efi"
