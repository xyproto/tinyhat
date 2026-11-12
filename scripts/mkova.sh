#!/bin/sh
set -eu
img=$1
out=$2
ostype=$3
version=$4
size=$5
[ -f "$img" ] && [ -n "$out" ] || exit 2

name="Tiny Hat $version (${img%.img})"
work="$out.work"
rm -rf "$work" "$out.tmp"
mkdir -p "$work/home"
export VBOX_USER_HOME="$PWD/$work/home"

cp --sparse=always "$img" "$work/disk.raw"
truncate -s "$size" "$work/disk.raw"
VBoxManage convertfromraw "$work/disk.raw" "$work/disk.vdi" --format VDI >/dev/null
rm -f "$work/disk.raw"

VBoxManage createvm --name "$name" --ostype "$ostype" --basefolder "$PWD/$work" --register >/dev/null
VBoxManage modifyvm "$name" --memory 4096 --cpus 2 --vram 128 --graphicscontroller vmsvga --accelerate-3d off \
	--firmware bios --rtc-use-utc on --mouse usbtablet --usb-ohci on \
	--audio-enabled on --audio-out on --audio-controller hda --audio-driver default \
	--nic1 nat --nic-type1 82540EM --clipboard-mode disabled
VBoxManage storagectl "$name" --name SATA --add sata --controller IntelAhci --portcount 1 --bootable on
VBoxManage storageattach "$name" --storagectl SATA --port 0 --device 0 --type hdd --medium "$PWD/$work/disk.vdi"
VBoxManage export "$name" --output "$PWD/$out.tmp.ova" --ovf10 --manifest --vsys 0 \
	--product "Tiny Hat" --version "$version" --vendor "Tiny Hat" \
	--producturl https://github.com/xyproto/tinyhat --vendorurl https://github.com/xyproto/tinyhat >/dev/null
VBoxManage unregistervm "$name" --delete >/dev/null
mv "$out.tmp.ova" "$out"
rm -rf "$work"
