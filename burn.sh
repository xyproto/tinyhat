#!/bin/sh

DRIVE=${1:-sdN}
EDITION=${2:-32}
DRIVE=${DRIVE#/dev/}
IMG=tinyhat$EDITION.img

fail() {
  echo "REFUSING: $*"
  exit 1
}

[ -f "$IMG" ] || fail "$IMG not found"
[ "$(od -An -tx1 -j440 -N4 "$IMG" | tr -d ' ')" = 00a71774 ] || fail "$IMG is not a Tiny Hat image"
[ -b "/dev/$DRIVE" ] && [ -d "/sys/block/$DRIVE" ] || fail "/dev/$DRIVE is not a whole drive"
case $(readlink -f "/sys/block/$DRIVE/device") in
*/usb*) ;;
*) [ "$(cat "/sys/block/$DRIVE/removable")" = 1 ] || fail "/dev/$DRIVE is not a USB or removable drive" ;;
esac
[ $(($(cat "/sys/block/$DRIVE/size") * 512)) -ge "$(stat -c %s "$IMG")" ] || fail "/dev/$DRIVE is too small"
[ $(($(cat "/sys/block/$DRIVE/size") / 2048)) -le 2000000 ] || fail "/dev/$DRIVE is larger than 2 TB"
for m in / /usr /boot /boot/efi /efi /home /var; do
  src=$(findmnt -nvo SOURCE -M "$m" 2>/dev/null) || continue
  lsblk -nslpo NAME "$src" 2>/dev/null | grep -qx "/dev/$DRIVE" && fail "/dev/$DRIVE holds $m"
done
swapon --noheadings --show=NAME 2>/dev/null | while read -r s; do
  lsblk -nslpo NAME "$s" 2>/dev/null | grep -qx "/dev/$DRIVE" && exit 1
done || fail "/dev/$DRIVE is used for swap"
if lsblk -nlpo MOUNTPOINTS "/dev/$DRIVE" | grep -q .; then
  lsblk -o NAME,SIZE,MOUNTPOINTS "/dev/$DRIVE"
  fail "/dev/$DRIVE has mounted partitions, unmount them first"
fi

echo '------------------------------------------------------------------------------------'
sudo fdisk -l
echo '------------------------------------------------------------------------------------'
echo
lsblk -o NAME,SIZE,TRAN,MODEL "/dev/$DRIVE"
echo "OVERWRITING /dev/$DRIVE with the ${EDITION}-bit edition, in 7 seconds!"
echo 'Press ctrl-c to cancel.'
echo
for n in 7 6 5 4 3 2 1; do
  echo $n
  sleep 1
done
echo "WRITING"
sudo dd if="$IMG" of="/dev/$DRIVE" bs=4M conv=fsync status=progress
echo SYNC
sync
sleep 1
echo DONE
