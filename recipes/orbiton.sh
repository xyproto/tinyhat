#!/bin/sh
set -eu
umask 022
rm -rf /work/orbiton /work/go /out/orbiton && mkdir -p /work/orbiton /work/go && cd /work/orbiton
tar --no-same-owner -xf /src/go1.26.8.linux-386.tar.gz -C /work/go --strip-components=1
tar --no-same-owner -xf /src/orbiton-fa8722804b6b75e697ea4ec8ea05c75420f07199.tar.gz --strip-components=1
cd v2
export GOROOT=/work/go GOPATH=/work/gopath GOCACHE=/work/gocache GOTOOLCHAIN=local GOFLAGS=-mod=vendor CGO_ENABLED=0 GOARCH=386 GO386=softfloat
/work/go/bin/go build -trimpath -buildvcs=false -ldflags "-s -w" -o /out/orbiton/usr/bin/o
for l in orbiton orbiton-nano nano feedgame obuild osudo vs; do ln -sf o /out/orbiton/usr/bin/$l; done
