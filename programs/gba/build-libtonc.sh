#!/bin/sh
set -eu
src=$1
out=$2
mkdir -p obj
for f in "$src"/asm/*.s "$src"/src/*.c "$src"/src/font/*.s "$src"/src/tte/*.c "$src"/src/tte/*.s "$src"/src/pre1.3/*.c "$src"/src/pre1.3/*.s; do
	[ -e "$f" ] || continue
	case $f in */tte_iohook.c) continue ;; esac
	o=obj/$(basename "$f").o
	case $f in
	*.iwram.c) arm-none-eabi-gcc -I"$src"/include -Wall -fno-strict-aliasing -O2 -mcpu=arm7tdmi -mthumb-interwork -marm -mlong-calls -c -o "$o" "$f" ;;
	*.c) arm-none-eabi-gcc -I"$src"/include -Wall -fno-strict-aliasing -O2 -mcpu=arm7tdmi -mthumb-interwork -mthumb -c -o "$o" "$f" ;;
	*.s) arm-none-eabi-gcc -x assembler-with-cpp -I"$src"/include -Wa,--warn -mcpu=arm7tdmi -mthumb-interwork -mthumb -c -o "$o" "$f" ;;
	esac
done
arm-none-eabi-ar rcs "$out" obj/*.o
