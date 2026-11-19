#!/bin/sh
set -eu
umask 022
mode=$1
top=$(dirname "$(dirname "$(readlink -f "$0")")")
root=${2:?}
sfs=${3:?}
lock=$(readlink -f "$4")
licdir=$(readlink -f "$5")
repo=$top/build/repo
section=image
arch=i686
strip=strip
bcj=x86
if grep -q -- '-x32\.pkg\.tar' "$lock"; then
  arch=x32
elif grep -q -- '-aarch64\.pkg\.tar' "$lock"; then
  arch=aarch64
  repo=$top/build/repo-aarch64
  section=image-aarch64
  strip=aarch64-linux-gnu-strip
  bcj=arm64
elif grep -q -- '-x86_64\.pkg\.tar' "$lock"; then
  arch=x86_64
  repo=$top/build/repo-x86_64
  section=image-x86_64
fi
cd "$top"

inlock() { awk -v p="$1" '$2 == p && $1 == "tinyhat" { found = 1 } END { exit !found }' "$lock"; }
localpkgs=
for p in $(python3 scripts/build_planner.py query tinyhat-pkgs); do
  if inlock "$p"; then
    localpkgs="$localpkgs $p"
  fi
done

case $mode in
  base)
    rm -rf "$root"
    rm -f "$root.etc.list"
    mkdir -p "$root"
    python3 scripts/pkg.py extract "$lock" "$repo" "$root" "$section"
    python3 scripts/pkg.py keep "$lock" "$repo" "$root" "$section"
    ln -sf clang "$root/usr/bin/cc"
    python3 scripts/mklocaldb.py "$lock" "$repo" "$root" "$section"

    boost=$(find "$root/usr/bin" "$root/usr/lib" -path "$root/usr/lib/modules" -prune -o -path "$root/usr/lib/firmware" -prune -o \
      -type f ! -name 'libboost_*' \( -perm -u+x -o -name '*.so*' \) -print0 \
      | xargs -0 readelf -d 2> /dev/null | sed -n 's/.*(NEEDED).*\[\(libboost_[^].]*\).*/\1/p' | sort -u)
    while :; do
      more=$(for b in $boost; do readelf -d "$root/usr/lib/$b".so.* 2> /dev/null; done \
        | sed -n 's/.*(NEEDED).*\[\(libboost_[^].]*\).*/\1/p' | sort -u)
      all=$(printf '%s\n' $boost $more | sort -u)
      [ "$all" = "$boost" ] && break
      boost=$all
    done
    for f in "$root"/usr/lib/libboost_*; do
      [ -e "$f" ] || [ -L "$f" ] || continue
      b=${f##*/}
      b=${b%%.*}
      case " $(echo $boost) " in *" $b "*) ;; *) rm -f "$f" ;; esac
    done
    if [ "$arch" = x32 ]; then
      ln -sfn usr/lib "$root/libx32"
      ln -sfn lib "$root/usr/libx32"
      mkdir -p "$root/etc/ld.so.conf.d"
      echo /usr/lib32 > "$root/etc/ld.so.conf.d/lib32.conf"
    fi
    for p in $(python3 scripts/distro.py packages image strip_unneeded); do
      for f in "$root"/$p; do
        [ ! -e "$f" ] || "$strip" --strip-unneeded "$f"
      done
    done

    for f in "$root"/usr/share/fontconfig/conf.default/*.conf; do
      case ${f##*/} in 10-sub-pixel-rgb.conf) continue ;; esac
      ln -sf "/usr/share/fontconfig/conf.default/${f##*/}" "$root/etc/fonts/conf.d/${f##*/}"
    done
    rm -f "$root/etc/fonts/conf.d/70-no-bitmaps-except-emoji.conf"

    python3 scripts/fwprune.py "$root"
    python3 - "$root"/usr/lib/systemd/libsystemd-core-*.so << 'EOF'
import sys
d = open(sys.argv[1], "rb").read()
old = b"\0\x1b[0;32m\0  OK  \0"
assert d.count(old) == 1, "systemd OK colour not found"
open(sys.argv[1], "wb").write(d.replace(old, b"\0\x1b[0;36m\0  OK  \0"))
EOF

    case $arch in
      i686 | x32) sed -i -E 's@^#(Server = https://(mirror\.archlinux32\.org|de\.mirror\.archlinux32\.org|archlinux32\.andreasbaumann\.cc|mirror\.math\.princeton\.edu)/)@\1@' "$root/etc/pacman.d/mirrorlist" ;;
      x86_64) sed -i -E 's@^#(Server = https://(geo\.mirror\.pkgbuild\.com|mirrors\.kernel\.org/archlinux)/)@\1@' "$root/etc/pacman.d/mirrorlist" ;;
    esac
    grep -q '^Server' "$root/etc/pacman.d/mirrorlist"
    sed -i 's|^HoldPkg .*|&\nNoExtract = usr/share/man/[a-z][a-z]/* usr/share/man/[a-z][a-z]_*/* usr/share/doc/* usr/share/info/* usr/share/locale/* usr/share/gtk-doc/* usr/share/help/*|' "$root/etc/pacman.conf"
    grep -q '^NoExtract' "$root/etc/pacman.conf"
    sed -i 's|^SigLevel *= *Required DatabaseOptional$|& TrustAll|' "$root/etc/pacman.conf"
    grep -q '^SigLevel *= *Required DatabaseOptional TrustAll$' "$root/etc/pacman.conf"
    ignore=$(
      python3 scripts/build_planner.py query built-pkgnames
      python3 scripts/distro.py pins
    )
    ignorepkgs=
    upgradable=" $(python3 scripts/distro.py unignored | tr '\n' ' ') "
    for p in $(printf '%s\n' $localpkgs $ignore | sort -u); do
      case $upgradable in *" $p "*) continue ;; esac
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
    manifest=$root.etc.list
    (cd config/etc && find . -mindepth 1 ! -type d | LC_ALL=C sort) > "$manifest.new"
    if [ -f "$manifest" ]; then
      comm -23 "$manifest" "$manifest.new" | while IFS= read -r p; do
        rm -rf "$root/etc/$p"
      done
    fi
    mv "$manifest.new" "$manifest"
    cp -a config/etc/. "$root/etc/"
    if [ -n "${TINYHAT_VERSION-}" ]; then
      sed -i "/^ID_LIKE=/a VERSION=\"$TINYHAT_VERSION\"\nVERSION_ID=$TINYHAT_VERSION" "$root/etc/os-release"
      sed -i "s/^title = \"Tiny Hat\"\$/title = \"Tiny Hat $TINYHAT_VERSION\"/" "$root/etc/tinyhat/dwl.toml"
    fi
    # Modes from the checkout depend on the umask of whoever cloned it (cp -a copies config/etc's own mode to /etc too)
    (cd config/etc && find .) | while IFS= read -r p; do
      if [ -d "config/etc/$p" ]; then
        chmod 755 "$root/etc/$p"
      else
        chmod 644 "$root/etc/$p"
      fi
    done
    mkdir -p "$root/usr/share/licenses/tinyhat"
    cp -a "$licdir/." "$root/usr/share/licenses/tinyhat/"

    touch -d 2026-10-05T00:00:00Z "$root/usr/lib/clock-epoch"
    grep -qx /usr/bin/fish "$root/etc/shells" || printf '/bin/fish\n/usr/bin/fish\n' >> "$root/etc/shells"
    term=$(sed -n 's/^term=//p' "$root/etc/xdg/foot/foot.ini")
    [ -f "$root/usr/share/terminfo/${term%"${term#?}"}/$term" ]

    scripts/inrootfs.sh "$root" /bin/sh -eu -c '
ldconfig
for d in /usr/lib/modules/*/; do
  [ ! -d "$d" ] || [ -f "${d}modules.dep" ] || depmod "$(basename "$d")"
done
systemd-sysusers >/dev/null
systemd-hwdb update --usr
fc-cache -s -f
update-ca-trust
if command -v glib-compile-schemas >/dev/null; then glib-compile-schemas /usr/share/glib-2.0/schemas; fi
if command -v gdk-pixbuf-query-loaders >/dev/null && [ -d /usr/lib/gdk-pixbuf-2.0/2.10.0 ]; then gdk-pixbuf-query-loaders --update-cache; fi
if command -v gio-querymodules >/dev/null; then gio-querymodules /usr/lib/gio/modules; fi
makewhatis /usr/share/man
if command -v update-mime-database >/dev/null; then update-mime-database /usr/share/mime; fi
if command -v update-desktop-database >/dev/null; then update-desktop-database -q /usr/share/applications; fi
for t in /usr/share/icons/*/; do
  [ -f "$t/index.theme" ] && gtk-update-icon-cache -q -f "$t" 2>/dev/null || true
done
rm -rf /home/tinyhat
if ! grep -q '^tinyhat:' /etc/passwd; then
  useradd -m -u 1000 -U -G wheel,audio,video,input,games,lp,storage,optical,uucp -s /usr/bin/fish tinyhat
fi
mkdir -p /home/tinyhat/downloads
cp -a /etc/skel/. /home/tinyhat/
chown -R tinyhat:tinyhat /home/tinyhat
chown tinyhat:tinyhat /home/tinyhat/downloads
echo tinyhat:tinyhat | chpasswd
passwd -l root >/dev/null
systemctl preset-all >/dev/null 2>&1
systemctl --global preset-all >/dev/null 2>&1
ln -sf /usr/lib/systemd/system/bluetooth.service /etc/systemd/system/dbus-org.bluez.service
ln -sf ../run/systemd/resolve/stub-resolv.conf /etc/resolv.conf
systemctl mask systemd-vconsole-setup.service systemd-nsresourced.socket systemd-nsresourced.service systemd-boot-update.service systemd-pcrmachine.service systemd-pcrphase.service systemd-pcrphase-sysinit.service ldconfig.service 2>/dev/null || true
systemctl --global mask gcr-ssh-agent.socket gcr-ssh-agent.service p11-kit-server.socket p11-kit-server.service 2>/dev/null || true
systemctl set-default multi-user.target
'

    : > "$root/etc/machine-id"
    rm -rf "$root/var/cache/"* "$root/var/log/"*
    mkdir -p "$root/media" "$root/persist" "$root/etc/dropbear" "$root/var/lib/tinyhat"

    python3 scripts/forbidcheck.py "$root"
    python3 scripts/sonamecheck.py "$root" "$lock" "$arch"
    python3 scripts/elfcheck.py "$root"
    scripts/inrootfs.sh "$root" /bin/sh -c "$(cat scripts/symcheck.sh)"

    find "$root/usr/share/man" -type l -name "*.gz" | while IFS= read -r l; do
      t=$(readlink "$l")
      ln -sfn "${t%.gz}" "${l%.gz}"
      rm -f "$l"
    done
    find "$root/usr/share/man" -name "*.gz" -type f -exec gunzip -f {} +

    python3 scripts/elfshrink.py "$root"
    rm -f "$sfs"
    python3 scripts/distro.py caps "$root" > "$sfs.caps"
    mksquashfs "$root" "$sfs" -comp xz -Xbcj "$bcj" -b 128K -noappend -quiet -no-progress \
      -xattrs-include '^security\.capability$' -pf "$sfs.caps" \
      -wildcards -e 'dev/*' 'proc/*' 'sys/*' 'run/*' 'tmp/*'
    rm -f "$sfs.caps"
    ;;
  *)
    echo "usage: $0 base|finalize root sfs lock licdir" >&2
    exit 2
    ;;
esac
