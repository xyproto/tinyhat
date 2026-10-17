#!/bin/sh
set -eu
umask 022
rm -rf /out/midibench && mkdir -p /out/midibench/usr/lib/tinyhat
cc $CFLAGS -o /out/midibench/usr/lib/tinyhat/midibench /programs/tinyhat-midibench.c $(pkg-config --cflags --libs fluidsynth) -lm $LDFLAGS
