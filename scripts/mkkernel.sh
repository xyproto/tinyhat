#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
export PATH="/usr/lib/ccache/bin:$PATH"
ver=$1
arch=$2
out=$3
shift 3
src=$top/build/linux-$ver-$arch
modules=$top/build/modules-$arch
xp=$top/build/xpadneo-$arch
if [ "$arch" = i386 ]; then
	src=$top/build/linux-$ver
	modules=$top/build/modules
fi
image=arch/x86/boot/bzImage
target=bzImage
dtbs=
case $arch-$(uname -m) in
arm64-aarch64) ;;
arm64-*) export CROSS_COMPILE="${CROSS_COMPILE-aarch64-linux-gnu-}" ;;
*-x86_64 | *-i?86) ;;
*) export CROSS_COMPILE="${CROSS_COMPILE-x86_64-linux-gnu-}" ;;
esac
if [ "$arch" = arm64 ]; then
	image=arch/arm64/boot/Image
	target=Image
	dtbs="broadcom/bcm2711-rpi-4-b.dtb broadcom/bcm2711-rpi-400.dtb broadcom/bcm2711-rpi-cm4-io.dtb"
fi
if [ ! -f "$src/Makefile" ] || [ ! -f "$src/scripts/Kbuild.include" ]; then
	rm -rf "$src"
	mkdir -p "$src"
	tar -xf "$top/vendor/src/linux-$ver.tar.xz" -C "$src" --strip-components=1
	patch -d "$src" -Np1 -s -i "$top/config/kernel-tinyhat-panic.patch"
fi
cd "$src"
make -s ARCH=$arch tinyconfig
./scripts/kconfig/merge_config.sh -m -O . .config "$@"
make -s ARCH=$arch olddefconfig
python3 "$top/scripts/kcheck.py" "$@" .config
mkdir -p "$top/build/chroot"
exec 6>"$top/build/chroot/.heavy"
flock 6
MAKEFLAGS= make -s ARCH=$arch -j"$(nproc)" KBUILD_BUILD_USER=tinyhat KBUILD_BUILD_HOST=tinyhat KBUILD_BUILD_TIMESTAMP="@${SOURCE_DATE_EPOCH:-315532800}" $target modules $dtbs
rm -rf "$modules"
make -s ARCH=$arch INSTALL_MOD_PATH="$modules/usr" INSTALL_MOD_STRIP=1 modules_install
rm -rf "$xp"
mkdir -p "$xp"
tar -xf "$top/vendor/src/xpadneo-0.10.5.tar.gz" -C "$xp" --strip-components=1
make -s ARCH=$arch M="$xp/hid-xpadneo/src" VERSION=v0.10.5 modules
make -s ARCH=$arch M="$xp/hid-xpadneo/src" INSTALL_MOD_PATH="$modules/usr" INSTALL_MOD_STRIP=1 modules_install
install -Dm644 "$xp/hid-xpadneo/etc-udev-rules.d/60-xpadneo.rules" "$modules/usr/lib/udev/rules.d/60-xpadneo.rules"
install -Dm644 "$xp/hid-xpadneo/etc-udev-rules.d/70-xpadneo-disable-hidraw.rules" "$modules/usr/lib/udev/rules.d/70-xpadneo-disable-hidraw.rules"
install -Dm644 "$xp/hid-xpadneo/etc-modprobe.d/xpadneo.conf" "$modules/usr/lib/modprobe.d/xpadneo.conf"
rm -f "$modules"/usr/lib/modules/*/build
cp "$image" "$top/$out"
if [ -n "$dtbs" ]; then
	rm -rf "$top/$out.dtbs"
	mkdir -p "$top/$out.dtbs"
	for d in $dtbs; do
		cp "arch/arm64/boot/dts/$d" "$top/$out.dtbs/"
	done
fi
# The build tree is only needed to rebuild the kernel, and takes a lot of space
if [ "${KEEP_SOURCES:-0}" != 1 ]; then
	cd "$top"
	rm -rf "$src" "$xp"
fi
