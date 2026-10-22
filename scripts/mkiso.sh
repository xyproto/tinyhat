#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
: "${SOURCE_DATE_EPOCH:=$(git -C "$top" log -1 --format=%ct 2>/dev/null || echo 315532800)}"
export SOURCE_DATE_EPOCH
img=$1
out=$2
work=$top/build/img/$(basename "$img" .img)
br=$top/build/buildroot
isod=$work/iso
tools=$work/isotools
esp_mib=16
pkg() { python3 "$top/scripts/pkg.py" repofiles "$top/build/repo" i686 "$1"; }
efi_modules="fat part_msdos part_gpt part_apple iso9660 efi_gop font gfxterm gfxmenu png search search_label normal linux echo test sleep reboot halt configfile cpuid loadenv smbios regexp"
pc_modules="biosdisk iso9660 search search_label normal linux linux16 chain vbe echo test sleep reboot halt configfile cpuid font gfxterm gfxmenu png loadenv smbios regexp"
if ! command -v xorriso >/dev/null 2>&1; then
	echo "xorriso not found in PATH (pacman -S libisoburn)" >&2
	exit 1
fi
[ -f "$work/root/boot/vmlinuz" ] || { echo "missing boot tree in $work (is $(basename "$img") built?)" >&2; exit 1; }
rm -rf "$isod" "$tools" "$out.tmp"
mkdir -p "$isod/boot/grub/i386-pc" "$isod/efi/boot" "$isod/.disk" "$tools/grub" "$tools/grub64"
cp -a "$work/root/." "$isod/"
uuid=$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y-%m-%d-%H-%M-%S-00)
: >"$isod/.disk/$uuid.uuid"
printf 'search --set=root --file /.disk/%s.uuid\nset prefix=($root)/boot/grub\n' "$uuid" >"$tools/eficfg"
printf '# tinyhat\n' >"$tools/pccfg"
bsdtar -xf "$(pkg grub)" -C "$tools/grub" usr/bin/grub-mkimage usr/lib/grub/i386-pc usr/lib/grub/i386-efi
if [ "$(basename "$img" .img)" = tinyhat64x32 ]; then
	bsdtar -xf "$(pkg grub-efi64)" -C "$tools" opt/grub-efi64/usr/bin/grub-mkimage opt/grub-efi64/usr/lib/grub/x86_64-efi
fi
$top/scripts/chroot.sh "$br" --bind "$work" /img \
	/img/isotools/grub/usr/bin/grub-mkimage -d /img/isotools/grub/usr/lib/grub/i386-efi -O i386-efi \
	-o /img/iso/efi/boot/bootia32.efi -c /img/isotools/eficfg -p /boot/grub $efi_modules
$top/scripts/chroot.sh "$br" --bind "$work" /img \
	/img/isotools/grub/usr/bin/grub-mkimage -d /img/isotools/grub/usr/lib/grub/i386-pc -O i386-pc-eltorito \
	-o /img/iso/boot/grub/i386-pc/eltorito.img -c /img/isotools/pccfg -p /boot/grub $pc_modules
if [ "$(basename "$img" .img)" = tinyhat64x32 ]; then
	$top/scripts/chroot.sh "$br" --bind "$work" /img \
		/img/isotools/grub64/opt/grub-efi64/usr/bin/grub-mkimage -d /img/isotools/grub64/opt/grub-efi64/usr/lib/grub/x86_64-efi -O x86_64-efi \
		-o /img/iso/efi/boot/bootx64.efi -c /img/isotools/eficfg -p /boot/grub $efi_modules
fi
truncate -s "${esp_mib}M" "$isod/efi.img"
$top/scripts/chroot.sh "$br" --bind "$work" /img \
	/bin/sh -c 'mkfs.fat --invariant -F 16 /img/iso/efi.img >/dev/null && mcopy -s -i /img/iso/efi.img /img/iso/efi ::/'
find "$isod" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
xorriso -as mkisofs -rational-rock \
	--modification-date="$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y%m%d%H%M%S00)" \
	-V TINYHAT -A TINYHAT \
	-b boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table \
	--grub2-boot-info --grub2-mbr "$tools/grub/usr/lib/grub/i386-pc/boot_hybrid.img" \
	--efi-boot efi.img -efi-boot-part --efi-boot-image \
	-o "$out.tmp" "$isod"
mv "$out.tmp" "$out"
rm -rf "$isod" "$tools"
