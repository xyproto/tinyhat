#!/bin/sh
set -eu
umask 077
dir=$1
[ -f "$dir/tinyhat.key" ] && [ -f "$dir/tinyhat.crt" ] && [ -f "$dir/tinyhat.cer" ] && exit 0
if [ -e "$dir/tinyhat.key" ] || [ -e "$dir/tinyhat.crt" ]; then
	echo "$dir has an incomplete Secure Boot key, refusing to replace it" >&2
	exit 1
fi
mkdir -p "$dir"
openssl req -new -x509 -newkey rsa:2048 -nodes -sha256 -days 36500 \
	-subj "/CN=Tiny Hat Secure Boot/" -addext "extendedKeyUsage=codeSigning" \
	-keyout "$dir/tinyhat.key" -out "$dir/tinyhat.crt" 2>/dev/null
openssl x509 -in "$dir/tinyhat.crt" -outform DER -out "$dir/tinyhat.cer"
echo "Created a new Secure Boot signing key in $dir"
echo "Back it up: images signed with a different key must be enrolled again on every PC."
