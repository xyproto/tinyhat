#!/bin/sh
set -eu
img=$1
out=$2
size=$3
[ -f "$img" ] && [ -n "$out" ] || exit 2

name=${out%.tar.gz}
work="$out.work"
rm -rf "$work" "$out.tmp"
mkdir -p "$work/$name"

qemu-img convert -c -O qcow2 "$img" "$work/$name/$name.qcow2"
qemu-img resize -q "$work/$name/$name.qcow2" "$size"

cat >"$work/$name/run.sh" <<EOF
#!/bin/sh
cd "\$(dirname "\$0")"
fw=
for f in "\${QEMU_EFI-}" /usr/share/edk2/aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd \\
	/usr/share/qemu-efi-aarch64/QEMU_EFI.fd /usr/share/edk2/aarch64/QEMU_CODE.fd \\
	/usr/share/qemu/edk2-aarch64-code.fd /opt/homebrew/share/qemu/edk2-aarch64-code.fd \\
	/usr/local/share/qemu/edk2-aarch64-code.fd; do
	[ -n "\$f" ] && [ -f "\$f" ] && fw=\$f && break
done
[ -n "\$fw" ] || { echo "No aarch64 UEFI firmware found. Install edk2 for aarch64 (edk2-aarch64, qemu-efi-aarch64 or qemu), or set QEMU_EFI=/path/to/QEMU_EFI.fd" >&2; exit 1; }
accel="-accel tcg -cpu max"
case "\$(uname -s)/\$(uname -m)" in
Linux/aarch64) [ -w /dev/kvm ] && accel="-accel kvm -cpu host" ;;
Darwin/arm64) accel="-accel hvf -cpu host" ;;
esac
code=\$fw
case \$fw in *CODE*) vars=\${TMPDIR:-/tmp}/$name-vars.fd; [ -f "\$vars" ] || dd if=/dev/zero of="\$vars" bs=1M count=64 2>/dev/null ;; *) vars= ;; esac
if [ -n "\$vars" ]; then
	firmware="-drive if=pflash,format=raw,readonly=on,file=\$code -drive if=pflash,format=raw,file=\$vars"
else
	firmware="-bios \$fw"
fi
exec qemu-system-aarch64 -M virt \$accel -smp 4 -m 4096 \$firmware \\
	-drive if=none,id=stick,file=$name.qcow2,format=qcow2 \\
	-device qemu-xhci -device usb-storage,drive=stick,bootindex=0 \\
	-device usb-kbd -device usb-tablet -device virtio-gpu-pci \\
	-nic user,model=virtio-net-pci "\$@"
EOF
chmod 755 "$work/$name/run.sh"
tar -C "$work" -czf "$out.tmp" "$name"
mv "$out.tmp" "$out"
rm -rf "$work"
