#!/bin/sh
set -eu
umask 022
CFLAGS=$(echo "$CFLAGS" | sed 's/-Os/-O3/')
CXXFLAGS=$(echo "$CXXFLAGS" | sed 's/-Os/-O3/')
LDFLAGS=$(echo "$LDFLAGS" | sed "s/,--gc-sections//")
export CFLAGS CXXFLAGS LDFLAGS
rm -rf /work/scummvm && mkdir -p /work/scummvm && cd /work/scummvm
tar --no-same-owner -xf /src/scummvm-2026.3.0.tar.xz --strip-components=1
for p in /patches/scummvm/*.patch; do patch -Np1 -i "$p"; done
mkdir -p pc
for f in /usr/lib/pkgconfig/*.pc /usr/share/pkgconfig/*.pc; do
	case ${f##*/} in sdl.pc | sdl2.pc | SDL2_*.pc | sdl2-compat.pc) continue ;; esac
	ln -sf "$f" pc/
done
export PKG_CONFIG_LIBDIR=/work/scummvm/pc
STRINGS=strings ./configure --prefix=/usr --backend=sdl --enable-release-mode --disable-optimizations --disable-debug \
	--disable-tts --disable-cloud --disable-libcurl --disable-discord --disable-eventrecorder \
	--disable-gtk --disable-sndio --disable-updates --disable-vpx --disable-taskbar --disable-text-console \
	--disable-sdlnet --disable-all-engines \
	--enable-engine=scumm,scumm_7_8,sci,sci32,agi,agos,kyra,lol,eob,queen,sky,sword1,sword2,lure,drascula,gob,saga,ihnm,saga2,cine,cruise,made,tinsel,touche,tucker,parallaction,groovie,tsage,access,mads,sherlock,cge,cge2,hugo,dreamweb,lab,mortevielle,teenagent,draci,chewy,supernova,voyeur,mm,xeen,mm1,zvision,twine,dgds,freescape,m4,trecision,darkseed,hopkins,awe,toltecs,lastexpress,glk,ultima,ultima4,ultima6,ultima8,efh,got
make
make install DESTDIR=/out/scummvm
