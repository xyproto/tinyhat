#!/bin/sh
set -eu
umask 022
rm -rf /work/sdl3-man /out/sdl3-man && mkdir -p /work/sdl3-man && cd /work/sdl3-man
for t in SDL3-3.4.18 SDL3_image-3.4.8 SDL3_ttf-3.2.2; do
	mkdir "$t" "/work/sdl3-man/wiki-$t"
	mkdir -p /out/sdl3-man/usr/share/man
	tar --no-same-owner -xf "/src/$t.tar.gz" -C "$t" --strip-components=1
	(cd "$t" && perl build-scripts/wikiheaders.pl . /work/sdl3-man/wiki-$t --options=.wikiheaders-options --copy-to-wiki >/dev/null &&
		perl build-scripts/wikiheaders.pl . /work/sdl3-man/wiki-$t --options=.wikiheaders-options --manpath=/out/sdl3-man/usr/share/man --copy-to-manpages >/dev/null)
done
