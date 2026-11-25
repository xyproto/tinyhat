#!/bin/sh
srcdir=$1
shift
printf 'extern const unsigned char *help_text[];\n\n'
i=0
for f in "$@"; do
	printf 'static const unsigned char help_text_%d[] = {\n' "$i"
	tr -d '\r' <"$srcdir/$f" | grep -v '^$' | sed 's/¶/\x14/g; s/§/\x15/g' | iconv -f UTF-8 -t CP437 | od -An -v -tx1 | tr -s ' \n' '\n' | grep . | sed 's/^/0x/; s/$/, /' | tr -d '\n' || exit 1
	printf '0x00};\n\n'
	i=$((i + 1))
done
printf 'const unsigned char* help_text[] = {\n'
j=0
while [ "$j" -lt "$i" ]; do
	printf '\thelp_text_%d,\n' "$j"
	j=$((j + 1))
done
printf '};\n'
