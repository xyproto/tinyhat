#!/bin/sh
umask 022
exec unshare --map-auto --map-root-user --mount --pid --fork --mount-proc "$@"
