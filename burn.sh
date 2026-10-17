#!/bin/sh
echo
sudo fdisk -l
echo
echo
DRIVE=${1:-sdN}
echo "OVERWRITING /dev/$DRIVE in 5 seconds! Press ctrl-c to cancel."
for n in 7 6 5 4 3 2 1; do
  echo $n
  sleep 1
done
echo WRITING
sudo dd if=tinyhat32.img of="/dev/$DRIVE" bs=4M conv=fsync status=progress
echo SYNC
sync
echo DONE
