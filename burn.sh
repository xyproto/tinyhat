#!/bin/sh

DRIVE=${1:-sdN}
EDITION=${2:-32}
DRIVE=${DRIVE#/dev/}

fail() {
  echo "REFUSING: $*"
  exit 1
}

if [ -f "$EDITION" ]; then
  IMG=$EDITION
else
  case $EDITION in
    32 | i686) NAME=tinyhat32 ;;
    64 | x86_64) NAME=tinyhat64 ;;
    rpi4) NAME=tinyhat-rpi4 ;;
    aarch64 | arm64) NAME=tinyhat-aarch64 ;;
    *) fail "unknown edition $EDITION (use 32, 64, rpi4, aarch64 or the path of an .img or .img.gz file)" ;;
  esac
  IMG=build/$NAME.img
  if [ ! -f "$IMG" ]; then
    IMG=$(ls -t build/dist/"$NAME"-*.img.gz 2>/dev/null | head -n 1)
    [ -n "$IMG" ] || fail "neither build/$NAME.img nor build/dist/$NAME-*.img.gz was found"
  fi
  if [ "$DRIVE" = sdN ] && [ "$EDITION" = rpi4 ] && [ -b /dev/mmcblk0 ]; then
    DRIVE=mmcblk0
  fi
fi

case $IMG in
  *.gz)
    command -v gzip >/dev/null || fail "gzip is needed for $IMG"
    gzip -t "$IMG" || fail "$IMG is not a valid gzip file"
    read_img() { gzip -dc "$IMG"; }
    SIZE=$(gzip -dc "$IMG" | wc -c)
    ;;
  *)
    read_img() { cat "$IMG"; }
    SIZE=$(stat -c %s "$IMG")
    ;;
esac

[ "$(read_img 2>/dev/null | head -c 444 | od -An -tx1 -j440 -N4 | tr -d ' ')" = 00a71774 ] || fail "$IMG is not a Tiny Hat image"
[ -b "/dev/$DRIVE" ] && [ -d "/sys/block/$DRIVE" ] || fail "/dev/$DRIVE is not a whole drive"
case $DRIVE in
  mmcblk*)
    [ "$(cat "/sys/block/$DRIVE/device/type" 2>/dev/null)" = SD ] || fail "/dev/$DRIVE is not an SD card (built-in eMMC storage is never overwritten)"
    ;;
  *)
    [ "$(lsblk -dno TRAN "/dev/$DRIVE")" = usb ] || [ "$(lsblk -dno RM "/dev/$DRIVE")" = 1 ] || fail "/dev/$DRIVE is not a USB or removable drive"
    ;;
esac
[ "$(lsblk -dbno SIZE "/dev/$DRIVE")" -ge "$SIZE" ] || fail "/dev/$DRIVE is too small"
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
read_img | sudo dd of="/dev/$DRIVE" bs=4M iflag=fullblock oflag=direct conv=fsync status=progress && sync && echo DONE
