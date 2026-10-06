#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
root=$1
modules=$2
sfs=$3
out=$top/build/buildroot/out
cd "$top"

rm -rf "$root"
mkdir -p "$root"
python3 scripts/pkg.py extract rootfs.lock vendor/pkg "$root" config/rootfs.exclude
pkgfile() { echo "vendor/pkg/$(awk -v p="$1" '$2 == p { print $4 }' rootfs.lock)"; }
bsdtar -xpf "$(pkgfile glibc)" -C "$root" 'usr/include/*' 'usr/lib/*.o' usr/lib/libc_nonshared.a
bsdtar -xpf "$(pkgfile tcc)" -C "$root" 'usr/lib/tcc/*'
for p in libdom-0.4.1-2.1 libhubbub-0.3.7-2.1 libparserutils-0.2.4-5.1 libnsutils-0.1.0-2.1 libnsbmp-0.1.6-8.1; do
	bsdtar -xpf "vendor/src/$p-i686.pkg.tar.zst" -C "$root" 'usr/lib/*.so*'
done
rm -f "$root/usr/lib/libdom.so.0.4.2" "$root/usr/lib/libhubbub.so.0.3.8" "$root/usr/lib/libparserutils.so.0.2.5" \
	"$root/usr/lib/libnsutils.so.0.1.1" "$root/usr/lib/libnsbmp.so.0.1.7"
bsdtar -xpf "$(pkgfile gcc)" -C "$root" 'usr/lib/gcc/i686-pc-linux-gnu/*/crt*.o' 'usr/lib/gcc/i686-pc-linux-gnu/*/libgcc*.a'
ln -sf clang "$root/usr/bin/cc"
bsdtar -xpf "$(pkgfile archlinux-wallpaper)" -C "$root" usr/share/backgrounds/archlinux/small.png
bsdtar -xpf "$(pkgfile terminus-font)" -C "$root" usr/share/kbd/consolefonts/ter-v24n.psf.gz usr/share/kbd/consolefonts/ter-v24b.psf.gz
for p in linux-api-headers libxcrypt; do
	bsdtar -xpf "$(pkgfile $p)" -C "$root" 'usr/include/*'
done
python3 scripts/mklocaldb.py rootfs.lock vendor/pkg "$root" config/rootfs.txt

chmod -R u=rwX,go=rX "$out" "$modules"
find "$root/usr/lib" -maxdepth 1 -name "libboost_*" ! -name "libboost_iostreams*" ! -name "libboost_filesystem*" ! -name "libboost_program_options*" ! -name "libboost_atomic*" -delete
for o in sdl3 sdl2-compat zsnes dosbox-x scummvm libdisplay-info wlroots dwl fuzzel swaylock swaybg orbiton sdl2_image sdl3_image sdl3_ttf grafx2 raylib sdl3-man fontconfig libxml2 libxml2-legacy fluidsynth gdb cc65 wordgrinder; do
	cp -a "$out/$o/." "$root/"
done
rm -rf "$root/usr/lib/cmake" "$root/usr/share/metainfo" "$root/usr/share/doc" "$root/usr/share/gettext"
rm -rf "$root"/usr/include/libxml2 "$root"/usr/bin/xml2-config "$root"/usr/include/wlroots-* "$root"/usr/include/libdisplay-info "$root"/usr/share/wayland-sessions
find "$root/usr/lib/pkgconfig" -type f ! -name sdl3.pc ! -name sdl3-image.pc ! -name sdl3-ttf.pc ! -name raylib.pc ! -name fluidsynth.pc -delete
sed -i "/^Requires.private:/d" "$root/usr/lib/pkgconfig/fluidsynth.pc"
rm -rf "$root"/usr/include/SDL2 "$root"/usr/bin/sdl2-config "$root"/usr/share/aclocal
rm -f "$root"/usr/share/scummvm/fonts-cjk.dat "$root"/usr/share/scummvm/fonts-imgui.dat
find "$root/usr/share/applications" -type f ! -name "tinyhat-*.desktop" -delete
sstrip "$root/usr/bin/zsnes" "$root/usr/bin/dosbox-x" "$root/usr/bin/scummvm" "$root/usr/bin/dwl" "$root/usr/bin/fuzzel" "$root/usr/bin/grafx2-sdl3"
strip --strip-unneeded "$root"/usr/lib/libSDL3.so.*.* "$root"/usr/lib/libSDL2-2.0.so.*.* "$root"/usr/lib/libwlroots-*.so "$root"/usr/lib/libdisplay-info.so.*.* "$root"/usr/lib/libfluidsynth.so.*.* "$root"/usr/lib/libSDL3_image.so.*.* "$root"/usr/lib/libSDL3_ttf.so.*.* "$root"/usr/lib/libraylib.so.*.* "$root"/usr/lib/libfontconfig.so.*.*

localpkgs=
while IFS='|' read -r o name ver lic url provides desc; do
	python3 scripts/mklocalpkg.py "$root" "$out/$o" "$name" "$ver" "$desc" "$url" "$lic" "$provides"
	localpkgs="$localpkgs $name"
done <<'EOF'
sdl3|sdl3|3.4.18-1|Zlib|https://libsdl.org/||Simple DirectMedia Layer 3 with the Tiny Hat Wayland framebuffer patch
sdl2-compat|sdl2-compat|2.32.74-1|Zlib|https://github.com/libsdl-org/sdl2-compat|sdl2=2.32.74|SDL2 API on top of SDL3
sdl2_image|sdl2_image|2.8.12-1|Zlib|https://github.com/libsdl-org/SDL_image||SDL2 image loading library, built small for LibreSprite
sdl3_image|sdl3_image|3.4.8-1|Zlib|https://github.com/libsdl-org/SDL_image||SDL3 image loading library (PNG, JPEG, GIF, BMP and more)
sdl3_ttf|sdl3_ttf|3.2.2-1|Zlib|https://github.com/libsdl-org/SDL_ttf||SDL3 TrueType font library
sdl3-man|sdl3-man|3.4.18-1|Zlib|https://libsdl.org/||Man pages for SDL3, SDL3_image and SDL3_ttf
raylib|raylib|6.0-1|Zlib|https://www.raylib.com/||raylib 6.0 on SDL3 and OpenGL 2.1
libdisplay-info|libdisplay-info|0.3.0-1|MIT|https://gitlab.freedesktop.org/emersion/libdisplay-info||EDID and DisplayID library
wlroots|wlroots0.19|0.19.3-1|MIT|https://gitlab.freedesktop.org/wlroots/wlroots||Modular Wayland compositor library
dwl|dwl|0.8-1|GPL-3.0-only|https://codeberg.org/dwl/dwl||dwm for Wayland, with the Tiny Hat patches
fuzzel|fuzzel|1.8.1-1|MIT|https://codeberg.org/dnkl/fuzzel||Application launcher for Wayland
swaylock|swaylock|1.8.6-1|MIT|https://github.com/swaywm/swaylock||Screen locker for Wayland
swaybg|swaybg|1.2.2-1|MIT|https://github.com/swaywm/swaybg||Wallpaper tool for Wayland, with the Tiny Hat help text patch
zsnes|zsnes|2.3.6-1|GPL-2.0-or-later|https://github.com/xyproto/zsnes||Super Nintendo emulator
dosbox-x|dosbox-x|2026.10.01-1|GPL-2.0-or-later|https://dosbox-x.com/||DOS emulator, with the Tiny Hat patches
scummvm|scummvm|2026.3.0-1|GPL-3.0-or-later|https://www.scummvm.org/||Engine for classic adventure games
fluidsynth|fluidsynth|2.6.1-1|LGPL-2.1-or-later|https://www.fluidsynth.org/||SoundFont synthesizer with PipeWire output
orbiton|orbiton|0+4bdfcf9f-1|BSD-3-Clause|https://orbiton.zip/||Editor and IDE, with Fennel support
grafx2|grafx2|0+f84cb09d-1|GPL-2.0-only|http://grafx2.eu/||Pixel art paint program, ported to SDL3
fontconfig|fontconfig|2:2.18.3-1|HPND AND MIT|https://www.freedesktop.org/wiki/Software/fontconfig/||Font configuration library
libxml2|libxml2|2.15.1-5|MIT|https://gitlab.gnome.org/GNOME/libxml2||XML library, built without ICU
libxml2-legacy|libxml2-legacy|2.13.9-1|MIT|https://gitlab.gnome.org/GNOME/libxml2||XML library with the older libxml2.so.2 ABI
gdb|gdb|17.1-1|GPL-3.0-or-later|https://www.sourceware.org/gdb/||GNU debugger, without Python
cc65|cc65|2.19+71746c8-1|Zlib|https://cc65.github.io/||6502 and 65816 C compiler and assembler
wordgrinder|wordgrinder|0.8-1|MIT|http://cowlark.com/wordgrinder/||Word processor for the terminal
EOF

for f in "$root"/usr/share/fontconfig/conf.default/*.conf; do
	case ${f##*/} in 10-sub-pixel-rgb.conf) continue ;; esac
	ln -sf "/usr/share/fontconfig/conf.default/${f##*/}" "$root/etc/fonts/conf.d/${f##*/}"
done
rm -f "$root/etc/fonts/conf.d/70-no-bitmaps-except-emoji.conf"

cp -a "$modules/." "$root/"

install -Dm644 vendor/soundfont/TinyHat-GM-0.790.sf3 "$root/usr/share/soundfonts/TinyHat-GM-0.790.sf3"
install -Dm644 vendor/soundfont/GeneralUser-GS-2.0.3.sf3 "$root/usr/share/soundfonts/GeneralUser-GS-2.0.3.sf3"
ln -sf GeneralUser-GS-2.0.3.sf3 "$root/usr/share/soundfonts/default.sf2"
mkdir -p "$root/usr/share/games/fotaq"
python3 -m zipfile -e vendor/src/FOTAQ_Talkie-1.1.zip "$root/usr/share/games/fotaq"
chmod 644 "$root"/usr/share/games/fotaq/*
mkdir -p "$root/usr/lib/watcom"
bsdtar -xf vendor/src/open-watcom-2026-10-01.tar.xz -C "$root/usr/lib/watcom" \
	--exclude './binl/*.sym' --exclude './binl/w??axp' --exclude './binl/wccppc' --exclude './binl/wccmps' \
	--exclude './binl/wfc*' --exclude './binl/wipfc' --exclude './h/nt' --exclude './h/os2' --exclude './h/os21x' --exclude './h/win' \
	--exclude './lib286/os2' --exclude './lib286/win' --exclude './lib386/nt' --exclude './lib386/os2' --exclude './lib386/win' \
	--exclude './lib386/linux' --exclude './lib386/netware' --exclude './lib386/rdos*' \
	./binl ./h ./lib286 ./lib386 ./binw/dos4gw.exe ./binw/dos32a.exe \
	./binw/pmodew.exe ./binw/wstub.exe ./binw/wstubq.exe ./binw/stub32a.exe ./binw/stub32c.exe ./license.txt
chmod -R u=rwX,go=rX "$root/usr/lib/watcom"
mkdir -p "$root/usr/share/tinyhat/c-guide"
python3 -m zipfile -e vendor/src/beej-bgc.zip "$root/usr/share/tinyhat/c-guide"
python3 -m zipfile -e vendor/src/beej-bgclr.zip "$root/usr/share/tinyhat/c-guide"
chmod -R u=rwX,go=rX "$root/usr/share/tinyhat/c-guide"
python3 - "$root"/usr/lib/systemd/libsystemd-core-*.so <<'EOF'
import sys
d = open(sys.argv[1], "rb").read()
old = b"\0\x1b[0;32m\0  OK  \0"
assert d.count(old) == 1, "systemd OK colour not found"
open(sys.argv[1], "wb").write(d.replace(old, b"\0\x1b[0;36m\0  OK  \0"))
EOF

cp -a config/overlay/. "$root/"
(cd config/overlay && find . -mindepth 1) | while read -r p; do
	if [ -d "config/overlay/$p" ] || [ -x "config/overlay/$p" ]; then
		chmod 755 "$root/$p"
	else
		chmod 644 "$root/$p"
	fi
done
mkdir -p "$root/usr/share/licenses/tinyhat"
cp -a licenses/. "$root/usr/share/licenses/tinyhat/"


sed -i 's|^#\(Server = https://mirror.archlinux32.org/\)|\1|' "$root/etc/pacman.d/mirrorlist"
grep -q '^Server' "$root/etc/pacman.d/mirrorlist"
sed -i 's|^HoldPkg .*|&\nNoExtract = usr/share/man/[a-z][a-z]/* usr/share/man/[a-z][a-z]_*/* usr/share/doc/* usr/share/info/* usr/share/locale/* usr/share/gtk-doc/* usr/share/help/*|' "$root/etc/pacman.conf"
grep -q '^NoExtract' "$root/etc/pacman.conf"
sed -i "s|^#IgnorePkg *=.*|IgnorePkg =$localpkgs|" "$root/etc/pacman.conf"
grep -q '^IgnorePkg = sdl3 ' "$root/etc/pacman.conf"

touch -d 2026-10-05T00:00:00Z "$root/usr/lib/clock-epoch"
ln -sf doas "$root/usr/bin/sudo"
ln -sf dbclient "$root/usr/bin/ssh"
ln -sf drill "$root/usr/bin/dig"
ln -sf tinyhat-firewall "$root/usr/bin/ufw"
grep -qx /usr/bin/fish "$root/etc/shells" || printf '/bin/fish\n/usr/bin/fish\n' >>"$root/etc/shells"
chmod 4755 "$root/usr/bin/doas"
chmod 0400 "$root/etc/doas.conf"

scripts/inroot.sh "$root" /bin/sh -eu -c '
ldconfig
systemd-sysusers >/dev/null
systemd-hwdb update --usr
fc-cache -s -f
update-ca-trust
if command -v glib-compile-schemas >/dev/null; then glib-compile-schemas /usr/share/glib-2.0/schemas; fi
if command -v gdk-pixbuf-query-loaders >/dev/null; then gdk-pixbuf-query-loaders --update-cache; fi
if command -v gio-querymodules >/dev/null; then gio-querymodules /usr/lib/gio/modules; fi
makewhatis /usr/share/man
if command -v update-mime-database >/dev/null; then update-mime-database /usr/share/mime; fi
for t in /usr/share/icons/*/; do
	[ -f "$t/index.theme" ] && gtk-update-icon-cache -q -f "$t" 2>/dev/null || true
done
useradd -m -u 1000 -U -G wheel,audio,video,input,games,lp,storage,optical -s /usr/bin/fish tinyhat
echo tinyhat:tinyhat | chpasswd
passwd -l root >/dev/null
systemctl preset-all >/dev/null 2>&1
systemctl --global preset-all >/dev/null 2>&1
systemctl enable nftables.service tinyhat-firewall.service tinyhat-wifi-autostart.service systemd-networkd.service systemd-resolved.service earlyoom.service tinyhat-bluetooth.service tinyhat-pacman-init.service tinyhat-zram.service tinyhat-persist.service
systemctl disable bluetooth.service systemd-userdbd.socket systemd-networkd-wait-online.service 2>/dev/null || true
ln -sf /usr/lib/systemd/system/bluetooth.service /etc/systemd/system/dbus-org.bluez.service
ln -sf ../run/systemd/resolve/stub-resolv.conf /etc/resolv.conf
systemctl mask systemd-nsresourced.socket systemd-nsresourced.service systemd-boot-update.service systemd-pcrmachine.service systemd-pcrphase.service systemd-pcrphase-sysinit.service ldconfig.service 2>/dev/null || true
systemctl --global enable pipewire.socket wireplumber.service
systemctl --global disable syncthing.service mpris-proxy.service 2>/dev/null || true
systemctl --global mask gcr-ssh-agent.socket gcr-ssh-agent.service 2>/dev/null || true
systemctl set-default multi-user.target
'

: >"$root/etc/machine-id"
rm -rf "$root/var/cache/"* "$root/var/log/"*
mkdir -p "$root/media" "$root/persist" "$root/etc/dropbear" "$root/var/lib/tinyhat"

python3 scripts/elfcheck.py "$root"
scripts/inroot.sh "$root" /bin/sh -c "$(cat scripts/symcheck.sh)"

rm -f "$sfs"
mksquashfs "$root" "$sfs" -comp xz -Xbcj x86 -b 256K -noappend -quiet -no-progress -no-xattrs \
	-wildcards -e 'dev/*' 'proc/*' 'sys/*' 'run/*' 'tmp/*'
