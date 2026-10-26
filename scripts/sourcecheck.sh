#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
rc=0
checked=0
missing=0
while read -r file sum url; do
	case $file in ''|\#*) continue ;; esac
	checked=$((checked + 1))
	src=$top/vendor/src/$file
	if [ -f "$src" ]; then
		have=$(sha256sum "$src" | cut -d' ' -f1)
		if [ "$have" != "$sum" ]; then
			echo "$file: checksum mismatch (expected $sum, got $have)" >&2
			rc=1
		fi
	else
		missing=$((missing + 1))
		echo "$file: missing from vendor/src (run make sources)" >&2
	fi
	ok=
	for try in 1 2 3; do
		if curl -sfIL --max-time 30 -o /dev/null "$url" ||
			curl -sfL --max-time 30 -r 0-0 -o /dev/null "$url"; then
			ok=1
			break
		fi
		sleep 2
	done
	if [ -z "$ok" ]; then
		echo "$file: URL is not reachable: $url" >&2
		rc=1
	fi
done <<EOF
$(python3 "$top/scripts/distro.py" sources)
EOF
if [ "$rc" = 0 ]; then
	echo "sources: ok ($checked entries, $missing missing locally)"
fi
exit $rc
