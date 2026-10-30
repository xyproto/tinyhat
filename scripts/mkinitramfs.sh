#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
out=$1
arch=${2:-i686}
kernel=${3:-linux-rpi}
repo=$top/build/repo
libs=$repo
ld=usr/lib/ld-linux.so.2
work=$top/build/initramfs
if [ "$arch" = aarch64 ]; then
	libs=$top/build/repo-aarch64
	ld=usr/lib/ld-linux-aarch64.so.1
	work=$top/build/$(basename "$out" .img)
fi
rm -rf "$work"
mkdir -p "$work/bin" "$work/dev" "$work/proc" "$work/sys" "$work/run" "$work/newroot"
pkgfile() { python3 "$top/scripts/pkg.py" repofiles "$1" "$arch" "$2"; }
x() {
	p=$1
	shift
	bsdtar -xf "$(pkgfile "$libs" "$p")" -C "$work" "$@"
}
for p in busybox tinyhat-splash; do
	bsdtar -xf "$(pkgfile "$repo" "$p")" -C "$work" "usr/bin/$p"
	mv "$work/usr/bin/$p" "$work/bin/$p"
	chmod 755 "$work/bin/$p"
done
rm -rf "$work/usr"
x util-linux usr/bin/sfdisk usr/bin/partx usr/bin/wipefs
x util-linux-libs 'usr/lib/libblkid.so.1*' 'usr/lib/libfdisk.so.1*' 'usr/lib/libsmartcols.so.1*' 'usr/lib/libuuid.so.1*'
x xfsprogs usr/bin/mkfs.xfs
x glibc usr/lib/libc.so.6 "$ld"
x libinih 'usr/lib/libinih.so.0*'
x liburcu 'usr/lib/liburcu.so.8*' 'usr/lib/liburcu-common.so.8*'
x ncurses 'usr/lib/libncursesw.so.6*'
x readline 'usr/lib/libreadline.so.8*'
ln -s usr/lib "$work/lib"
if [ "$arch" = aarch64 ]; then
	kpkg=$(pkgfile "$libs" "$kernel")
	kdir=$work-kmod
	rm -rf "$kdir"
	mkdir -p "$kdir" "$work/modules"
	mods=$(bsdtar -tf "$kpkg" | grep '/kernel/.*\.ko\.\(xz\|zst\)$')
	order=
	add() {
		case " $order " in *" $1 "*) return ;; esac
		set -- "$1" "$(printf '%s\n' "$mods" | grep "/$(printf '%s' "$1" | sed 's/[-_]/[-_]/g')\.ko\.\(xz\|zst\)$" | head -n 1)" "${2-}"
		if [ -z "$2" ]; then
			[ -n "$3" ] && return
			echo "mkinitramfs.sh: no kernel module $1 in $kpkg" >&2
			exit 1
		fi
		bsdtar -xf "$kpkg" -C "$kdir" "$2"
		for d in $(modinfo -F depends "$kdir/$2" | tr , ' '); do
			add "$d"
		done
		case $2 in
		*.zst) zstd -q -dc "$kdir/$2" >"$work/modules/$1.ko" ;;
		*) xz -dc "$kdir/$2" >"$work/modules/$1.ko" ;;
		esac
		order="$order $1"
	}
	for m in xfs squashfs overlay exfat ntfs3; do
		add "$m"
	done
	if [ "$kernel" != linux-rpi ]; then
		for m in virtio_pci virtio_blk virtio_scsi nvme ahci ahci_platform xhci_pci xhci_plat_hcd usb_storage uas sd_mod; do
			add "$m" optional
		done
	fi
	printf '%s\n' $order >"$work/modules/order"
	rm -rf "$kdir"
fi
install -m755 "$top/config/initramfs/init" "$work/init"
find "$work" -exec touch -h -d "@${SOURCE_DATE_EPOCH:-315532800}" {} +
(cd "$work" && find . -mindepth 1 | LC_ALL=C sort | cpio --quiet --reproducible -o -H newc -R 0:0) | zstd -q -19 -T0 -f -o "$out"
