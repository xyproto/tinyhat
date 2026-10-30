#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
: "${SOURCE_DATE_EPOCH:=$(git -C "$top" log -1 --format=%ct 2>/dev/null || echo 315532800)}"
export SOURCE_DATE_EPOCH
ver=$1
shift
home=${DIST_HOME:-https://github.com/xyproto/tinyhat}
base=${DIST_URL:-$home/releases/download}
out=$top/build/dist
system=$top/build/system
stage=$out/src/tinyhat-write-macos-$ver
tgz() {
	tar -C "$out/src" --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
		-cf - "$1" | gzip -n -9 >"$out/$1.tar.gz"
	rm -rf "$out/src"
}
mkdir -p "$out" "$system"
rm -rf "$out/src"
mkdir -p "$stage"
cp "$top/programs/tinyhat-write.c" "$top/programs/tinyhat-write-core.h" "$top/programs/tinyhat-write-mac.m" "$stage/"
tgz "tinyhat-write-macos-$ver"
sha=$(sha256sum "$out/tinyhat-write-macos-$ver.tar.gz" | awk '{ print $1 }')
files="tinyhat-write-macos-$ver.tar.gz"
lic=$out/src/tinyhat-licenses-$ver
mkdir -p "$lic"
cp -a "$top/build/licenses" "$lic/tinyhat32"
[ -d "$top/build/licenses-x32" ] && cp -a "$top/build/licenses-x32" "$lic/tinyhat64x32"
[ -d "$top/build/licenses-aarch64" ] && cp -a "$top/build/licenses-aarch64" "$lic/tinyhat-aarch64"
[ -d "$top/build/licenses-rpi4" ] && cp -a "$top/build/licenses-rpi4" "$lic/tinyhat-rpi4"
tgz "tinyhat-licenses-$ver"
files="$files tinyhat-licenses-$ver.tar.gz"
x86_64-w64-mingw32-windres "$top/programs/tinyhat-write-win32.rc" -o "$out/win-res.o"
cd "$out"

for img in "$@"; do
	img=$(cd "$top" && readlink -f "$img")
	case ${img##*/} in
	*.iso)
		name=${img##*/}; name=${name%.iso}
		cp "$img" "$name-$ver.iso"
		files="$files $name-$ver.iso"
		continue
		;;
	*.vdi)
		name=${img##*/}; name=${name%.vdi}
		cp "$img" "$name-$ver.vdi"
		files="$files $name-$ver.vdi"
		continue
		;;
	tinyhat64x32*) name=tinyhat64x32 prog=tinyhat64x32 formula=tinyhat64x32-writer class=Tinyhat64x32Writer app="Tiny Hat 64x32 Writer" ;;
	tinyhat-aarch64*) name=tinyhat-aarch64 prog=tinyhat-aarch64 formula=tinyhat-aarch64-writer class=TinyhatAarch64Writer app="Tiny Hat ARM Writer" ;;
	tinyhat-rpi4*) name=tinyhat-rpi4 prog=tinyhat-rpi4 formula=tinyhat-rpi4-writer class=TinyhatRpi4Writer app="Tiny Hat Raspberry Pi 4 Writer" ;;
	*) name=tinyhat32 prog=tinyhat formula=tinyhat-writer class=TinyhatWriter app="Tiny Hat Writer" ;;
	esac
	cmd=$prog-write
	gz=$out/$name-$ver.img.gz
	if [ ! -f "$gz" ] || [ "$img" -nt "$gz" ]; then
		gzip -n -c -9 "$img" >"$gz.tmp"
		mv "$gz.tmp" "$gz"
	fi
	ln -f "$gz" "$out/img.gz"
	x86_64-w64-mingw32-gcc -Os -s -static -mwindows -DEMBED_IMG -DTW_VERSION="\"$ver\"" \
		-Wl,--no-insert-timestamp \
		-o "$out/$prog-$ver.exe" "$top/programs/tinyhat-write-win32.c" "$out/win-res.o" -lcomctl32 -lshell32
	cc -Os -s -static-libgcc -DEMBED_IMG -DTW_VERSION="\"$ver\"" \
		-o "$out/$prog-$ver-linux-x86_64" "$top/programs/tinyhat-write.c" \
		$(pkg-config --cflags --libs gtk4)
	rm -f "$out/img.gz"
	imgsha=$(sha256sum "$gz" | awk '{ print $1 }')
	cat >"$system/$formula.rb" <<EOF
class $class < Formula
  desc "Write Tiny Hat images to USB sticks"
  homepage "$home"
  url "$base/v$ver/tinyhat-write-macos-$ver.tar.gz"
  sha256 "$sha"

  depends_on :macos

  resource "image" do
    url "$base/v$ver/$name-$ver.img.gz", using: :nounzip
    sha256 "$imgsha"
  end

  def install
    app = prefix/"$app.app/Contents"
    system ENV.cc, "-O2", "-fobjc-arc", "-DTW_VERSION=\\"$ver\\"", "tinyhat-write-mac.m", "-o", "tinyhat-write",
           "-framework", "Cocoa", "-framework", "IOKit", "-framework", "DiskArbitration"
    (app/"MacOS").install "tinyhat-write"
    resource("image").stage { (app/"Resources").install "$name-$ver.img.gz" => "tinyhat.img.gz" }
    (app/"Info.plist").write <<~PLIST
      <?xml version="1.0" encoding="UTF-8"?>
      <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
      <plist version="1.0">
      <dict>
        <key>CFBundleExecutable</key><string>tinyhat-write</string>
        <key>CFBundleIdentifier</key><string>io.github.xyproto.$formula</string>
        <key>CFBundleName</key><string>$app</string>
        <key>CFBundlePackageType</key><string>APPL</string>
        <key>CFBundleShortVersionString</key><string>$ver</string>
        <key>CFBundleVersion</key><string>$ver</string>
        <key>LSMinimumSystemVersion</key><string>10.13</string>
        <key>NSHighResolutionCapable</key><true/>
      </dict>
      </plist>
    PLIST
    bin.install_symlink app/"MacOS/tinyhat-write" => "$cmd"
  end

  def caveats
    <<~TEXT
      Start it with: $cmd
      To get it in Launchpad and Finder:
        ln -sf "#{opt_prefix}/$app.app" /Applications/
    TEXT
  end

  test do
    assert_match "tinyhat-write", shell_output("#{bin}/$cmd --version")
  end
end
EOF
	files="$files $name-$ver.img.gz $prog-$ver.exe $prog-$ver-linux-x86_64"
done
rm -f "$out/win-res.o"

rm -f SHA256SUMS.asc
sha256sum $files >SHA256SUMS
ls -l $files SHA256SUMS
