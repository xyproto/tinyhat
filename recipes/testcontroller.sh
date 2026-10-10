#!/bin/sh
set -eu
umask 022
rm -rf /work/testcontroller /out/testcontroller && mkdir -p /work/testcontroller /out/testcontroller/usr/bin && cd /work/testcontroller
tar --no-same-owner -xf /src/SDL3-3.4.18.tar.gz --strip-components=1 SDL3-3.4.18/test
cc $CFLAGS -Itest -o /out/testcontroller/usr/bin/testcontroller \
	test/testcontroller.c test/gamepadutils.c test/testutils.c -lSDL3_test -lSDL3 -lm $LDFLAGS
