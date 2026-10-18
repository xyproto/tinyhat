#!/bin/sh

DRIVE=${1:-sdN}
EDITION=${2:-32}
DRIVE=${DRIVE#/dev/}
case $EDITION in
32) IMG=tinyhat32.img ;;
64 | x32 | 64x32) IMG=tinyhat64x32.img ;;
*)
  echo "REFUSING: unknown edition $EDITION (use 32, 64, x32 or 64x32)"
  exit 1
  ;;
esac

fail() {
  echo "REFUSING: $*"
  exit 1
}

[ -f "$IMG" ] || fail "$IMG not found"
[ "$(od -An -tx1 -j440 -N4 "$IMG" | tr -d ' ')" = 00a71774 ] || fail "$IMG is not a Tiny Hat image"
[ -b "/dev/$DRIVE" ] && [ -d "/sys/block/$DRIVE" ] || fail "/dev/$DRIVE is not a whole drive"
[ "$(lsblk -dno TRAN "/dev/$DRIVE")" = usb ] || [ "$(lsblk -dno RM "/dev/$DRIVE")" = 1 ] || fail "/dev/$DRIVE is not a USB or removable drive"
[ "$(lsblk -dbno SIZE "/dev/$DRIVE")" -ge "$(stat -c %s "$IMG")" ] || fail "/dev/$DRIVE is too small"
if lsblk -nlo MOUNTPOINTS "/dev/$DRIVE" | grep -q .; then
  lsblk -o NAME,SIZE,MOUNTPOINTS "/dev/$DRIVE"
  fail "/dev/$DRIVE is in use (mounted or swap), unmount it first"
fi

lsblk -o NAME,SIZE,TRAN,MODEL "/dev/$DRIVE"
echo
echo "OVERWRITING /dev/$DRIVE with $IMG, in 7 seconds! Press ctrl-c to cancel."
for n in 7 6 5 4 3 2 1; do
  echo $n
  sleep 1
done
sudo dd if="$IMG" of="/dev/$DRIVE" bs=4M conv=fsync status=progress && sync && echo DONE
