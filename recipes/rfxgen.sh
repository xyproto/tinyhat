#!/bin/sh
set -eu
umask 022
rm -rf /work/rfxgen /out/rfxgen && mkdir -p /work/rfxgen /out/rfxgen/usr/bin && cd /work/rfxgen
tar --no-same-owner -xf /src/rfxgen-5.0.tar.gz --strip-components=1
cc $CFLAGS -std=gnu99 -DCUSTOM_MODAL_DIALOGS -D_DEFAULT_SOURCE -Isrc/external \
	-o /out/rfxgen/usr/bin/rfxgen src/rfxgen.c -lraylib -lm $LDFLAGS
