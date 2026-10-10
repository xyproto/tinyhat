#!/bin/sh
set -eu
umask 022
rm -rf /out/retro && mkdir -p /out/retro/usr/bin
cc $CFLAGS -I/programs/retro -o /out/retro/usr/bin/tinyhat-retro /programs/retro/tinyhat-retro.c -lSDL3 $LDFLAGS
