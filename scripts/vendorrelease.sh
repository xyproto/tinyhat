#!/bin/sh
set -eu
top=$(dirname "$(dirname "$(readlink -f "$0")")")
tag=vendor
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
command -v gh >/dev/null || { echo "vendorrelease.sh: gh is not installed" >&2; exit 1; }
python3 "$top/scripts/pkg.py" release-files | while read -r path; do
	ln -s "$path" "$stage/$(basename "$path")"
done
gh release view "$tag" >/dev/null 2>&1 ||
	gh release create "$tag" --target main --title "Vendored packages" --notes "" --latest=false
have=$(gh release view "$tag" --json assets -q '.assets[].name')
set -- "$stage"/*
[ -e "$1" ] || exit 0
missing=
for f do
	case "$have" in
		*"$(basename "$f")"*) ;;
		*) missing="$missing $f" ;;
	esac
done
if [ -n "$missing" ]; then
	gh release upload "$tag" $missing
else
	echo "vendorrelease.sh: every pinned package is already on the $tag release"
fi
