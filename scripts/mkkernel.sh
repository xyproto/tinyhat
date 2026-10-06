#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
ver=$1
frag=$2
src=$top/build/linux-$ver
modules=$top/build/modules
if [ ! -f "$src/Makefile" ]; then
	rm -rf "$src"
	tar -xf "$top/vendor/src/linux-$ver.tar.xz" -C "$top/build"
fi
cd "$src"
make -s ARCH=i386 tinyconfig
./scripts/kconfig/merge_config.sh -m -O . .config "$frag" >/dev/null
make -s ARCH=i386 olddefconfig
python3 "$top/scripts/kcheck.py" "$frag" .config
make -s ARCH=i386 -j"$(nproc)" KBUILD_BUILD_USER=tinyhat KBUILD_BUILD_HOST=tinyhat bzImage modules
rm -rf "$modules"
make -s ARCH=i386 INSTALL_MOD_PATH="$modules/usr" INSTALL_MOD_STRIP=1 modules_install
rm -rf "$top/build/xpadneo"
mkdir -p "$top/build/xpadneo"
tar -xf "$top/vendor/src/xpadneo-0.10.4.tar.gz" -C "$top/build/xpadneo" --strip-components=1
make -s ARCH=i386 M="$top/build/xpadneo/hid-xpadneo/src" VERSION=v0.10.4 modules
make -s ARCH=i386 M="$top/build/xpadneo/hid-xpadneo/src" INSTALL_MOD_PATH="$modules/usr" INSTALL_MOD_STRIP=1 modules_install
install -Dm644 "$top/build/xpadneo/hid-xpadneo/etc-udev-rules.d/60-xpadneo.rules" "$modules/usr/lib/udev/rules.d/60-xpadneo.rules"
install -Dm644 "$top/build/xpadneo/hid-xpadneo/etc-udev-rules.d/70-xpadneo-disable-hidraw.rules" "$modules/usr/lib/udev/rules.d/70-xpadneo-disable-hidraw.rules"
install -Dm644 "$top/build/xpadneo/hid-xpadneo/etc-modprobe.d/xpadneo.conf" "$modules/usr/lib/modprobe.d/xpadneo.conf"
rm -f "$modules"/usr/lib/modules/*/build
cp arch/x86/boot/bzImage "$top/build/vmlinuz"
