#!/bin/sh
set -eu
umask 022
root=$1
shift
mkdir -p "$root/dev" "$root/proc" "$root/sys" "$root/tmp" "$root/run"
mount --rbind /dev "$root/dev"
mount -t proc proc "$root/proc"
mount -t tmpfs tmpfs "$root/tmp"
mount -t tmpfs tmpfs "$root/run"
rc=0
env -i PATH=/usr/bin HOME=/root LANG=C.UTF-8 setarch i686 chroot "$root" "$@" || rc=$?
umount -lR "$root/dev" "$root/proc" "$root/tmp" "$root/run" 2>/dev/null || true
exit $rc
