#!/bin/sh
umask 022
auto=--map-auto
for f in /etc/subuid /etc/subgid; do
	grep -qs -e "^$(id -un):" -e "^$(id -u):" "$f" || auto=
done
exec unshare $auto --map-root-user --mount --pid --fork --mount-proc "$@"
