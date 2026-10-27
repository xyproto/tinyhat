#!/bin/sh
set -eu
umask 022
mode=$1
top=$(dirname "$(dirname "$(readlink -f "$0")")")
root=$2
modules=$3
sfs=$4
lock=$(readlink -f "$5")
licdir=$(readlink -f "$6")
repo=$top/build/repo
arch=i686
if grep -q -- '-x32\.pkg\.tar' "$lock"; then
	arch=x32
fi
cd "$top"

inlock() { awk -v p="$1" '$2 == p && $1 == "tinyhat" { found = 1 } END { exit !found }' "$lock"; }
localpkgs=
for p in ${TINYHAT_PKGS-}; do
	if inlock "$p"; then
		localpkgs="$localpkgs $p"
	fi
done

case $mode in
base)
	rm -rf "$root"
	mkdir -p "$root"
	python3 scripts/pkg.py extract "$lock" "$repo" "$root" image
	pkgfile() { echo "$repo/$(awk -v p="$1" '$2 == p && $1 != "lib32" { print $4 }' "$lock")"; }
	bsdtar -xpf "$(pkgfile glibc)" -C "$root" 'usr/include/*' 'usr/lib/*.o' usr/lib/libc_nonshared.a
	bsdtar -xpf "$(pkgfile tcc)" -C "$root" 'usr/lib/tcc/*'
	bsdtar -xpf "$(pkgfile gcc)" -C "$root" 'usr/lib/gcc/i686-pc-linux-gnu/*/crt*.o' 'usr/lib/gcc/i686-pc-linux-gnu/*/libgcc*.a'
	a=usr/lib/gcc/arm-none-eabi/16.2.0
	bsdtar -xpf "$(pkgfile arm-none-eabi-gcc)" -C "$root" "$a/cc1" "$a/collect2" "$a/liblto_plugin.so" "$a/include" "$a/include-fixed" "$a/libgcc.a" "$a/crtbegin.o" "$a/crtend.o" "$a/crti.o" "$a/crtn.o" \
		"$a/thumb/nofp/libgcc.a" "$a/thumb/nofp/crtbegin.o" "$a/thumb/nofp/crtend.o" "$a/thumb/nofp/crti.o" "$a/thumb/nofp/crtn.o"
	ln -sf clang "$root/usr/bin/cc"
	bsdtar -xpf "$(pkgfile archlinux-wallpaper)" -C "$root" usr/share/backgrounds/archlinux/small.png
	mkdir -p "$root/usr/share/backgrounds/tinyhat"
	python3 scripts/mkwallpaper.py img/tinyhat.svg "$root/usr/share/backgrounds/tinyhat/tinyhat.png"
	python3 scripts/mkhatwallpaper.py "$root/usr/share/backgrounds/tinyhat/wirehat.png"
	python3 scripts/mkhundetann.py "$root/usr/share/backgrounds/tinyhat/hundetann.png"
	python3 scripts/mkpixelitos.py vendor/src/pixelitos-icon-theme-20260721.tar.gz "$root/usr/share/icons/pixelitos-dark"
	bsdtar -xpf "$(pkgfile terminus-font)" -C "$root" usr/share/kbd/consolefonts/ter-v24n.psf.gz usr/share/kbd/consolefonts/ter-v24b.psf.gz
	for p in linux-api-headers libxcrypt libglvnd glu glew glfw; do
		bsdtar -xpf "$(pkgfile $p)" -C "$root" 'usr/include/*'
	done
	for p in libglvnd glu glew glfw; do
		bsdtar -xpf "$(pkgfile $p)" -C "$root" 'usr/lib/pkgconfig/*'
	done
	python3 scripts/mklocaldb.py "$lock" "$repo" "$root" image
	for p in ${TINYHAT_PKGS-}; do
		if inlock "$p"; then
			bsdtar --no-xattrs --no-acls --no-fflags -xpf "$(pkgfile "$p")" -C "$root" \
				--exclude .PKGINFO --exclude .BUILDINFO --exclude .MTREE --exclude .INSTALL --exclude .CHANGELOG
		fi
	done

	chmod -R u=rwX,go=rX "$modules"
	find "$root/usr/lib" -maxdepth 1 -name "libboost_*" ! -name "libboost_iostreams*" ! -name "libboost_filesystem*" ! -name "libboost_program_options*" ! -name "libboost_atomic*" ! -name "libboost_container*" ! -name "libboost_random*" -delete
	if [ "$arch" = x32 ]; then
		ln -sfn usr/lib "$root/libx32"
		ln -sfn lib "$root/usr/libx32"
		mkdir -p "$root/etc/ld.so.conf.d"
		echo /usr/lib32 >"$root/etc/ld.so.conf.d/lib32.conf"
	fi
	rm -rf "$root/usr/lib/cmake" "$root/usr/share/metainfo" "$root/usr/share/doc" "$root/usr/share/gettext"
	rm -rf "$root/usr/share/icons/Adwaita" "$root/usr/share/icons/AdwaitaLegacy"
	rm -rf "$root"/usr/include/libxml2 "$root"/usr/bin/xml2-config "$root"/usr/include/wlroots-* "$root"/usr/include/libdisplay-info "$root"/usr/share/wayland-sessions
	find "$root/usr/lib/pkgconfig" -type f ! -name sdl3.pc ! -name sdl3-image.pc ! -name sdl3-ttf.pc ! -name raylib.pc ! -name fluidsynth.pc \
		! -name gl.pc ! -name opengl.pc ! -name glx.pc ! -name egl.pc ! -name glesv2.pc ! -name libglvnd.pc ! -name glu.pc ! -name glew.pc ! -name glfw3.pc -delete
	sed -i "/^Requires.private:/d" "$root/usr/lib/pkgconfig/fluidsynth.pc"
	rm -rf "$root"/usr/share/aclocal
	rm -f "$root"/usr/share/scummvm/fonts-cjk.dat "$root"/usr/share/scummvm/fonts-imgui.dat
	find "$root/usr/share/applications" -type f ! -name "tinyhat-*.desktop" -delete
	sstrip "$root/usr/bin/zsnes-bin" "$root/usr/bin/dosbox-x" "$root/usr/bin/scummvm" "$root/usr/bin/dwl" "$root/usr/bin/fuzzel" "$root/usr/bin/grafx2-sdl3" "$root/usr/bin/algernon" "$root/usr/bin/syncthing"
	sstrip "$root"/usr/lib/libgallium-*.so
	strip --strip-unneeded "$root"/usr/lib/libLLVM*.so* "$root"/usr/lib/libLTO*.so* "$root"/usr/lib/libRemarks*.so* "$root"/usr/lib/libclang-cpp*.so* "$root"/usr/lib/libSDL3.so.*.* "$root"/usr/lib/libwlroots-*.so "$root"/usr/lib/libdisplay-info.so.*.* "$root"/usr/lib/libfluidsynth.so.*.* "$root"/usr/lib/libSDL3_image.so.*.* "$root"/usr/lib/libSDL3_ttf.so.*.* "$root"/usr/lib/libraylib.so.*.* "$root"/usr/lib/libfontconfig.so.*.*

	for f in "$root"/usr/share/fontconfig/conf.default/*.conf; do
		case ${f##*/} in 10-sub-pixel-rgb.conf) continue ;; esac
		ln -sf "/usr/share/fontconfig/conf.default/${f##*/}" "$root/etc/fonts/conf.d/${f##*/}"
	done
	rm -f "$root/etc/fonts/conf.d/70-no-bitmaps-except-emoji.conf"

	cp -a "$modules/." "$root/"
	for k in "$modules"/usr/lib/modules/*/; do
		k=${k%/}
		k=${k##*/}
		db=$root/var/lib/pacman/local/linux-tinyhat-${k%-tinyhat}-1
		mkdir -p "$db"
		printf '%%NAME%%\nlinux-tinyhat\n\n%%VERSION%%\n%s-1\n\n%%DESC%%\nThe Tiny Hat Linux kernel\n\n%%ARCH%%\n%s\n\n%%PROVIDES%%\nNTSYNC-MODULE\n\n' "${k%-tinyhat}" "$arch" >"$db/desc"
		printf '%%FILES%%\n\n' >"$db/files"
	done

	install -Dm644 vendor/soundfont/GeneralUser-GS-2.0.3.sf3 "$root/usr/share/soundfonts/GeneralUser-GS-2.0.3.sf3"
	ln -sf GeneralUser-GS-2.0.3.sf3 "$root/usr/share/soundfonts/default.sf2"
	mkdir -p "$root/usr/share/games/bass"
	python3 -m zipfile -e vendor/src/BASS-Floppy-1.3.zip "$root/usr/share/games/bass"
	chmod 644 "$root"/usr/share/games/bass/*
	python3 -m zipfile -e vendor/src/lure-1.1.zip "$root/usr/share/games"
	for g in soltys-en-v1.0:soltys; do
		mkdir -p "$root/usr/share/games/${g#*:}"
		python3 -m zipfile -e "vendor/src/${g%:*}.zip" "$root/usr/share/games/${g#*:}"
	done
	chmod 755 "$root"/usr/share/games/lure "$root"/usr/share/games/soltys
	chmod 644 "$root"/usr/share/games/lure/* "$root"/usr/share/games/soltys/*
	mkdir -p "$root/usr/lib/watcom"
	bsdtar -xf vendor/src/open-watcom-2026-10-01.tar.xz -C "$root/usr/lib/watcom" \
		--exclude './binl/*.sym' --exclude './binl/vi' --exclude './binl/w??axp' --exclude './binl/wccppc' --exclude './binl/wccmps' \
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

	sed -i -E 's@^#(Server = https://(mirror\.archlinux32\.org|de\.mirror\.archlinux32\.org|archlinux32\.andreasbaumann\.cc|mirror\.math\.princeton\.edu)/)@\1@' "$root/etc/pacman.d/mirrorlist"
	grep -q '^Server' "$root/etc/pacman.d/mirrorlist"
	sed -i 's|^HoldPkg .*|&\nNoExtract = usr/share/man/[a-z][a-z]/* usr/share/man/[a-z][a-z]_*/* usr/share/doc/* usr/share/info/* usr/share/locale/* usr/share/gtk-doc/* usr/share/help/*|' "$root/etc/pacman.conf"
	grep -q '^NoExtract' "$root/etc/pacman.conf"
	ignore=$(
		for m in build/pkg/mode/*; do
			[ "$(cat "$m")" = built ] && [ -f "packages/${m##*/}/.SRCINFO" ] && sed -n 's/^pkgname = //p' "packages/${m##*/}/.SRCINFO"
		done
		python3 scripts/distro.py pins
	)
	ignorepkgs=
	for p in $(printf '%s\n' $localpkgs $ignore | sort -u); do
		inlock "$p" && ignorepkgs="$ignorepkgs $p"
	done
	sed -i "s|^#IgnorePkg *=.*|IgnorePkg =$ignorepkgs glfw-x11 glfw-wayland|" "$root/etc/pacman.conf"
	grep -q '^IgnorePkg = .* pt2-clone' "$root/etc/pacman.conf"
	sed -i 's|^CheckSpace$|&\nILoveCandy|' "$root/etc/pacman.conf"
	grep -qx ILoveCandy "$root/etc/pacman.conf"
	grep -q '^IgnorePkg = .* sdl3' "$root/etc/pacman.conf"
	sed -i "s|^Architecture = auto$|Architecture = $arch|" "$root/etc/pacman.conf"
	grep -q "^Architecture = $arch$" "$root/etc/pacman.conf"
	;;
finalize)
	manifest="$root.overlay.list"
	newmani=$(mktemp)
	(cd config/overlay && find . -mindepth 1 ! -type d | LC_ALL=C sort) >"$newmani"
	if [ -f "$manifest" ]; then
		comm -23 "$manifest" "$newmani" | while IFS= read -r p; do
			rm -rf "$root/$p"
		done
	fi
	cp "$newmani" "$manifest"
	rm -f "$newmani"
	cp -a config/overlay/. "$root/"
	(cd config/overlay && find . -mindepth 1) | while read -r p; do
		if [ -d "config/overlay/$p" ] || [ -x "config/overlay/$p" ]; then
			chmod 755 "$root/$p"
		else
			chmod 644 "$root/$p"
		fi
	done
	install -m644 -t "$root/etc/skel/cprojects/bootanim" packages/tinyhat-splash/hat.c packages/tinyhat-splash/hat.h
	for c in hat_color:HAT_COLOR hat_highlight_color:HAT_HIGHLIGHT_COLOR; do
		v=$(sed -n "s/^${c%%:*} = \"#\([0-9a-fA-F]\{6\}\)\"$/\1/p" build/theme.toml)
		sed -i "s/^#define ${c#*:} 0x.*/#define ${c#*:} 0x$v/" "$root/etc/skel/cprojects/bootanim/hat.h"
		grep -qx "#define ${c#*:} 0x$v" "$root/etc/skel/cprojects/bootanim/hat.h"
	done
	mkdir -p "$root/usr/share/licenses/tinyhat"
	cp -a "$licdir/." "$root/usr/share/licenses/tinyhat/"

	touch -d 2026-10-05T00:00:00Z "$root/usr/lib/clock-epoch"
	ln -sf doas "$root/usr/bin/sudo"
	ln -sf dbclient "$root/usr/bin/ssh"
	ln -sf drill "$root/usr/bin/dig"
	ln -sf tinyhat-firewall "$root/usr/bin/ufw"
	ln -sf cowsay "$root/usr/bin/cowthink"
	ln -sf poweroff "$root/usr/bin/natta"
	ln -sf poweroff "$root/usr/bin/nightynight"
	ln -sf sleepmode "$root/usr/bin/sov"
	ln -sf sleepmode "$root/usr/bin/suspend"
	grep -qx /usr/bin/fish "$root/etc/shells" || printf '/bin/fish\n/usr/bin/fish\n' >>"$root/etc/shells"
	term=$(sed -n 's/^term=//p' "$root/etc/xdg/foot/foot.ini")
	[ -f "$root/usr/share/terminfo/${term%"${term#?}"}/$term" ]
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
rm -rf /home/tinyhat
if ! grep -q '^tinyhat:' /etc/passwd; then
	useradd -m -u 1000 -U -G wheel,audio,video,input,games,lp,storage,optical -s /usr/bin/fish tinyhat
fi
mkdir -p /home/tinyhat/downloads
cp -a /etc/skel/. /home/tinyhat/
chown -R tinyhat:tinyhat /home/tinyhat
chown tinyhat:tinyhat /home/tinyhat/downloads
echo tinyhat:tinyhat | chpasswd
passwd -l root >/dev/null
systemctl preset-all >/dev/null 2>&1
systemctl --global preset-all >/dev/null 2>&1
systemctl enable nftables.service tinyhat-pci-latency.service tinyhat-firewall.service tinyhat-wifi-autostart.service systemd-networkd.service systemd-resolved.service earlyoom.service tinyhat-bluetooth.service tinyhat-pacman-init.service tinyhat-zram.service tinyhat-persist.service tinyhat-timesync-once.service tinyhat-pacman-restore.service tinyhat-pacman-sync.service tinyhat-midibench.service
systemctl disable bluetooth.service systemd-userdbd.socket systemd-networkd-wait-online.service 2>/dev/null || true
ln -sf /usr/lib/systemd/system/bluetooth.service /etc/systemd/system/dbus-org.bluez.service
ln -sf ../run/systemd/resolve/stub-resolv.conf /etc/resolv.conf
systemctl mask systemd-nsresourced.socket systemd-nsresourced.service systemd-boot-update.service systemd-pcrmachine.service systemd-pcrphase.service systemd-pcrphase-sysinit.service ldconfig.service 2>/dev/null || true
systemctl --global disable syncthing.service mpris-proxy.service filter-chain.service 2>/dev/null || true
systemctl --global enable pipewire.socket wireplumber.service
systemctl --global mask gcr-ssh-agent.socket gcr-ssh-agent.service p11-kit-server.socket p11-kit-server.service 2>/dev/null || true
systemctl set-default multi-user.target
'

	: >"$root/etc/machine-id"
	rm -rf "$root/var/cache/"* "$root/var/log/"*
	mkdir -p "$root/media" "$root/persist" "$root/etc/dropbear" "$root/var/lib/tinyhat"

	python3 scripts/elfcheck.py "$root"
	scripts/inroot.sh "$root" /bin/sh -c "$(cat scripts/symcheck.sh)"

	find "$root/usr/share/man" -type l -name "*.gz" | while IFS= read -r l; do
		t=$(readlink "$l")
		ln -sfn "${t%.gz}" "${l%.gz}"
		rm -f "$l"
	done
	find "$root/usr/share/man" -name "*.gz" -type f -exec gunzip -f {} +
	;;
*)
	echo "usage: $0 base|finalize root modules sfs lock licdir" >&2
	exit 2
	;;
esac

if [ "$mode" = finalize ]; then
	rm -f "$sfs"
	mksquashfs "$root" "$sfs" -comp xz -Xbcj x86 -b 256K -noappend -quiet -no-progress -no-xattrs \
		-wildcards -e 'dev/*' 'proc/*' 'sys/*' 'run/*' 'tmp/*'
fi
