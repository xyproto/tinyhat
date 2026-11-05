#!/bin/sh
# Writes the checksum into the SNES header, so that emulators see the ROM as intact.
rom=$1
sum=$(od -An -v -tu1 "$rom" | awk '{ for (i = 1; i <= NF; i++) s += $i } END { print s % 65536 }')
inv=$((sum ^ 65535))
printf "$(printf '\\%03o\\%03o\\%03o\\%03o' $((inv & 255)) $((inv >> 8)) $((sum & 255)) $((sum >> 8)))" |
	dd of="$rom" bs=1 seek=$((0x7FDC)) conv=notrunc 2>/dev/null
