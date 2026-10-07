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
if [ "$arch" = i386 ]; then
	src=$top/build/linux-$ver
	modules=$top/build/modules
fi
if [ ! -f "$src/Makefile" ]; then
	rm -rf "$src"
	mkdir -p "$src"
	tar -xf "$top/vendor/src/linux-$ver.tar.xz" -C "$src" --strip-components=1
fi
cd "$src"
make -s ARCH=$arch tinyconfig
./scripts/kconfig/merge_config.sh -m -O . .config "$@"
make -s ARCH=$arch olddefconfig
for f in "$@"; do
	python3 "$top/scripts/kcheck.py" "$f" .config
done
make -s ARCH=$arch -j"$(nproc)" KBUILD_BUILD_USER=tinyhat KBUILD_BUILD_HOST=tinyhat bzImage modules
rm -rf "$modules"
make -s ARCH=$arch INSTALL_MOD_PATH="$modules/usr" INSTALL_MOD_STRIP=1 modules_install
rm -rf "$top/build/xpadneo-$arch"
mkdir -p "$top/build/xpadneo-$arch"
tar -xf "$top/vendor/src/xpadneo-0.10.4.tar.gz" -C "$top/build/xpadneo-$arch" --strip-components=1
make -s ARCH=$arch M="$top/build/xpadneo-$arch/hid-xpadneo/src" VERSION=v0.10.4 modules
make -s ARCH=$arch M="$top/build/xpadneo-$arch/hid-xpadneo/src" INSTALL_MOD_PATH="$modules/usr" INSTALL_MOD_STRIP=1 modules_install
install -Dm644 "$top/build/xpadneo-$arch/hid-xpadneo/etc-udev-rules.d/60-xpadneo.rules" "$modules/usr/lib/udev/rules.d/60-xpadneo.rules"
install -Dm644 "$top/build/xpadneo-$arch/hid-xpadneo/etc-udev-rules.d/70-xpadneo-disable-hidraw.rules" "$modules/usr/lib/udev/rules.d/70-xpadneo-disable-hidraw.rules"
install -Dm644 "$top/build/xpadneo-$arch/hid-xpadneo/etc-modprobe.d/xpadneo.conf" "$modules/usr/lib/modprobe.d/xpadneo.conf"
rm -f "$modules"/usr/lib/modules/*/build
cp arch/x86/boot/bzImage "$top/$out"
