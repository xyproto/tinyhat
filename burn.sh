#!/bin/sh
DRIVE=${1:-sdN}
echo "OVERWRITING /dev/$DRIVE in 5 seconds! Press ctrl-c to cancel."
for n in 5 4 3 2 1; do
  echo $n
  sleep 1
done
echo GO
sudo dd if=tinyhat32.img of="/dev/$DRIVE" bs=4M conv=fsync status=progress
sync
echo DONE
