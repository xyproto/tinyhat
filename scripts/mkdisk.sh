#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
out=$1
root=$2
first=$3
type=$4
first_mib=$5
part=$out.part
content=$(du -sm --apparent-size "$root" | cut -f1)
part_mib=$((content + content / 32 + 64))
rm -f "$part"
truncate -s "${part_mib}M" "$part"
TEST_DIR=1 TEST_DEV=1 QA_CHECK_FS=xfs mkfs.xfs -q -f -L TINYHAT \
	-m crc=1,finobt=0,rmapbt=0,reflink=0,bigtime=1,inobtcount=0,metadir=0,uuid=7417a700-0000-4000-8000-000000007417 \
	-i nrext64=0,exchange=0 -n parent=0 -l size=64m \
	-p "$root" "$part"
python3 "$top/scripts/xfsnormalize.py" "$part" "${SOURCE_DATE_EPOCH:?}"

rm -f "$out"
truncate -s "$((part_mib + first_mib + 1))M" "$out"
printf 'label: dos\nlabel-id: 0x7417a700\nstart=2048, size=%d, type=%s\nstart=%d, type=83, bootable\n' \
	"$((first_mib * 2048))" "$type" "$((2048 + first_mib * 2048))" | sfdisk -q "$out"
dd if="$first" of="$out" bs=1M seek=1 conv=notrunc status=none
dd if="$part" of="$out" bs=1M seek="$((first_mib + 1))" conv=notrunc status=none
rm -f "$part"
