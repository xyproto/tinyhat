#!/bin/sh
out=$1
url=$2
name=${out%.part}
name=${name##*/}
top=$(dirname "$(dirname "$(readlink -f "$0")")")
try() {
	rm -f "$out"
	curl -qgb "" -fL --retry 2 --retry-delay 3 --connect-timeout 20 -o "$out" "$1"
}
for m in ${TINYHAT_SOURCE_MIRROR-}; do
	try "${m%/}/$name" && exit 0
done
while read -r from to; do
	case $from in '' | \#*) continue ;; esac
	case $url in
	"$from"*)
		for m in $to; do
			try "$m${url#"$from"}" && exit 0
		done
		;;
	esac
done <<EOF
$(python3 "$top/scripts/distro.py" mirrors)
EOF
try "$url"
