#!/bin/sh
set -eu
umask 022
rm -rf /work/png2xpm /work/go /out/png2xpm && mkdir -p /work/png2xpm /work/go /out/png2xpm/usr/bin
cd /work/png2xpm
tar --no-same-owner -xf /src/go1.26.8.linux-386.tar.gz -C /work/go --strip-components=1
tar --no-same-owner -xf /src/xpm-9eb268eca26e61eb7da0b1a9b25882b79195e9ab.tar.gz --strip-components=1
export GOROOT=/work/go GOPATH=/work/gopath GOCACHE=/work/gocache GOTOOLCHAIN=local GOFLAGS=-mod=vendor CGO_ENABLED=0 GOARCH=386 GO386=softfloat
/work/go/bin/go build -trimpath -buildvcs=false -ldflags "-s -w" -o /out/png2xpm/usr/bin/png2xpm ./cmd/png2xpm
