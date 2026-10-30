#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
: "${SOURCE_DATE_EPOCH:=$(git -C "$top" log -1 --format=%ct 2>/dev/null || echo 315532800)}"
export SOURCE_DATE_EPOCH
out=$1
kernel=$2
initrd=$3
sfs=$4
efi64=${5-}
sbkeys=${6-}
work=$top/build/img/$(basename "$out" .img)
br=$top/build/buildroot
esp_mib=16
efi_modules="fat part_msdos xfs search search_label normal linux echo test sleep reboot halt configfile cpuid efi_gop font gfxterm gfxmenu png loadenv smbios regexp"
pkg() { python3 "$top/scripts/pkg.py" repofile "$top/build/repo" i686 "$1"; }
rm -rf "$work"
mkdir -p "$work/root/boot/grub" "$work/root/tinyhat" "$work/grub" "$work/kbd"
cp "$kernel" "$work/root/boot/vmlinuz"
cp "$initrd" "$work/root/boot/initramfs.img"
bsdtar -xf "$(pkg amd-ucode)" -C "$work/root" boot/amd-ucode.img
bsdtar -xf "$(pkg intel-ucode)" -C "$work/root" boot/intel-ucode.img
bsdtar -xf "$(pkg grub)" -C "$work/grub" usr/bin/grub-mkimage usr/bin/grub-mkfont usr/lib/grub/i386-pc usr/lib/grub/i386-efi
bsdtar -xf "$(pkg kbd)" -C "$work/kbd" usr/share/kbd/consolefonts/default8x16.psfu.gz
cp "$top/config/grub.cfg" "$work/root/boot/grub/grub.cfg"
if [ -n "$efi64" ]; then
	echo "set tinyhat_arch=x86_64" >"$work/root/boot/grub/arch.cfg"
else
	echo "set tinyhat_arch=i686" >"$work/root/boot/grub/arch.cfg"
fi
bsdtar -xf "$(pkg memtest86+)" -C "$work/root" boot/memtest86+/memtest.bin
cp "$sfs" "$work/root/tinyhat/root.sfs"
cp "$top/config/grub-early.cfg" "$work/early.cfg"
theme=$work/root/boot/grub/themes/tinyhat
python3 "$top/scripts/mkgrubtheme.py" "$theme" "$work/kbd/usr/share/kbd/consolefonts/default8x16.psfu.gz"
for size in 16 32; do
	"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
		/img/grub/usr/bin/grub-mkfont -s "$size" -o "/img/root/boot/grub/themes/tinyhat/vga-$size.pf2" "/img/root/boot/grub/themes/tinyhat/vga-$size.bdf"
	rm -f "$theme/vga-$size.bdf"
done

"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
	/img/grub/usr/bin/grub-mkimage -d /img/grub/usr/lib/grub/i386-pc -O i386-pc -o /img/core.img -c /img/early.cfg -p /boot/grub \
	biosdisk part_msdos xfs search search_label normal linux linux16 chain vbe echo test sleep reboot halt configfile cpuid font gfxterm gfxmenu png fat loadenv smbios regexp
mkdir -p "$work/efi/EFI/BOOT" "$work/efi/tinyhat"
{
	echo "# GRUB Environment Block"
	head -c $((1024 - 25)) /dev/zero | tr '\0' '#'
} >"$work/efi/tinyhat/grubenv"
"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
	/img/grub/usr/bin/grub-mkimage -d /img/grub/usr/lib/grub/i386-efi -O i386-efi -o /img/efi/EFI/BOOT/BOOTIA32.EFI -c /img/early.cfg -p /boot/grub $efi_modules
if [ -n "$efi64" ]; then
	printf '%s\n' \
		"sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md" \
		"grub,5,Free Software Foundation,grub,2.14,https://www.gnu.org/software/grub/" \
		"grub.tinyhat,1,Tiny Hat,grub,2.14,https://github.com/xyproto/tinyhat" >"$work/sbat.csv"
	mkdir -p "$work/grub64"
	bsdtar -xf "$(pkg grub-efi64)" -C "$work/grub64" opt/grub-efi64/usr/bin/grub-mkimage opt/grub-efi64/usr/lib/grub/x86_64-efi
	"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
		/img/grub64/opt/grub-efi64/usr/bin/grub-mkimage -d /img/grub64/opt/grub-efi64/usr/lib/grub/x86_64-efi -O x86_64-efi --sbat /img/sbat.csv -o /img/grubx64.efi -c /img/early.cfg -p /boot/grub $efi_modules
	shim=usr/lib/efi/shim/16.1-10/EFI
	mkdir -p "$work/shim" "$work/sbs"
	bsdtar -xf "$top/vendor/src/shim-x64-16.1-10.x86_64.rpm" -C "$work/shim" "./$shim"
	sha256sum -c --quiet <<EOF
351e131d3c4a636704e9cae7d1296baf8a05beca9922cfebbfa48933c9bf1ad3  $work/shim/$shim/BOOT/BOOTX64.EFI
eb84ac15b99ebbabf9b7dd187c6a20b05fd3aca512ccf49b08922780d6cd42d8  $work/shim/$shim/fedora/mmx64.efi
EOF
	bsdtar -xf "$(pkg sbsigntools)" -C "$work/sbs" usr/bin/sbsign
	cp "$work/shim/$shim/BOOT/BOOTX64.EFI" "$work/efi/EFI/BOOT/BOOTX64.EFI"
	cp "$work/shim/$shim/fedora/mmx64.efi" "$work/efi/EFI/BOOT/mmx64.efi"
	mv "$work/root/boot/vmlinuz" "$work/vmlinuz.unsigned"
	"$top/scripts/chroot.sh" "$br" --bind "$work" /img --ro-bind "$sbkeys" /keys /bin/sh -c '
		/img/sbs/usr/bin/sbsign --key /keys/tinyhat.key --cert /keys/tinyhat.crt --output /img/efi/EFI/BOOT/grubx64.efi /img/grubx64.efi &&
		/img/sbs/usr/bin/sbsign --key /keys/tinyhat.key --cert /keys/tinyhat.crt --output /img/root/boot/vmlinuz /img/vmlinuz.unsigned'
	cp "$sbkeys/tinyhat.cer" "$work/efi/ENROLL_THIS_KEY_IN_MOKMANAGER.cer"
	rm -rf "$work/shim" "$work/sbs" "$work/grub64" "$work/grubx64.efi" "$work/vmlinuz.unsigned"
fi
cp "$work/grub/usr/lib/grub/i386-pc/boot.img" "$work/boot.img"
rm -rf "$work/grub" "$work/kbd"
find "$work" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
truncate -s "${esp_mib}M" "$work/esp.img"
"$top/scripts/chroot.sh" "$br" --bind "$work" /img \
	/bin/sh -ec 'mkfs.fat --invariant -F 16 /img/esp.img >/dev/null; mcopy -s -i /img/esp.img /img/efi/EFI ::/EFI; mcopy -s -i /img/esp.img /img/efi/tinyhat ::/tinyhat; for f in /img/efi/*.cer; do [ ! -f "$f" ] || mcopy -i /img/esp.img "$f" ::/; done'

rm -f "$out.tmp"
"$top/scripts/mkdisk.sh" "$out.tmp" "$work/root" "$work/esp.img" ef "$esp_mib"
python3 "$top/scripts/mbr.py" "$out.tmp" "$work/boot.img" "$work/core.img"
mv "$out.tmp" "$out"
rm -f "$work/esp.img"
rm -rf "$work/efi"
