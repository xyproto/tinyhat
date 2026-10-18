#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
: "${SOURCE_DATE_EPOCH:=$(git -C "$top" log -1 --format=%ct 2>/dev/null || echo 315532800)}"
export SOURCE_DATE_EPOCH
ver=$1
img=$2
home=${DIST_HOME:-https://github.com/xyproto/tinyhat}
base=${DIST_URL:-$home/releases/download}
out=$top/build/dist
stage=$out/src/tinyhat-write-$ver
mkdir -p "$out"
gz=$out/tinyhat32-$ver.img.gz
if [ ! -f "$gz" ] || [ "$img" -nt "$gz" ]; then
	gzip -n -c -9 "$img" >"$gz.tmp"
	mv "$gz.tmp" "$gz"
fi
ln -f "$gz" "$out/img.gz"
x86_64-w64-mingw32-windres "$top/programs/tinyhat-write-win32.rc" -o "$out/win-res.o"
cd "$out"
x86_64-w64-mingw32-gcc -Os -s -static -mwindows -DEMBED_IMG -DTW_VERSION="\"$ver\"" \
	-Wl,--no-insert-timestamp \
	-o "$out/tinyhat-$ver.exe" "$top/programs/tinyhat-write-win32.c" "$out/win-res.o" -lcomctl32 -lshell32
rm -f "$out/win-res.o"
cc -Os -s -static-libgcc -DEMBED_IMG -DTW_VERSION="\"$ver\"" \
	-o "$out/tinyhat-$ver-linux-x86_64" "$top/programs/tinyhat-write.c" \
	$(pkg-config --cflags --libs gtk+-3.0)
rm -f "$out/img.gz"
rm -rf "$out/src"
mkdir -p "$stage"
cp "$top/programs/tinyhat-write.c" "$top/programs/tinyhat-write-core.h" "$top/programs/tinyhat-write-mac.m" "$stage/"
tar -C "$out/src" --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
	-cf - "tinyhat-write-$ver" | gzip -n -9 >"$out/tinyhat-write-$ver.tar.gz"
sha=$(sha256sum "$out/tinyhat-write-$ver.tar.gz" | awk '{ print $1 }')
imgsha=$(sha256sum "$gz" | awk '{ print $1 }')
cat >"$out/tinyhat-writer.rb" <<EOF
class TinyhatWriter < Formula
  desc "Write Tiny Hat images to USB sticks"
  homepage "$home"
  url "$base/v$ver/tinyhat-write-$ver.tar.gz"
  sha256 "$sha"

  depends_on :macos

  resource "image" do
    url "$base/v$ver/tinyhat32-$ver.img.gz", using: :nounzip
    sha256 "$imgsha"
  end

  def install
    app = prefix/"Tiny Hat Writer.app/Contents"
    system ENV.cc, "-O2", "-fobjc-arc", "-DTW_VERSION=\\"$ver\\"", "tinyhat-write-mac.m", "-o", "tinyhat-write",
           "-framework", "Cocoa", "-framework", "IOKit", "-framework", "DiskArbitration"
    (app/"MacOS").install "tinyhat-write"
    resource("image").stage { (app/"Resources").install "tinyhat32-$ver.img.gz" => "tinyhat.img.gz" }
    (app/"Info.plist").write <<~PLIST
      <?xml version="1.0" encoding="UTF-8"?>
      <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
      <plist version="1.0">
      <dict>
        <key>CFBundleExecutable</key><string>tinyhat-write</string>
        <key>CFBundleIdentifier</key><string>io.github.xyproto.tinyhat-writer</string>
        <key>CFBundleName</key><string>Tiny Hat Writer</string>
        <key>CFBundlePackageType</key><string>APPL</string>
        <key>CFBundleShortVersionString</key><string>$ver</string>
        <key>CFBundleVersion</key><string>$ver</string>
        <key>LSMinimumSystemVersion</key><string>10.13</string>
        <key>NSHighResolutionCapable</key><true/>
      </dict>
      </plist>
    PLIST
    bin.install_symlink app/"MacOS/tinyhat-write"
  end

  def caveats
    <<~TEXT
      Start it with: tinyhat-write
      To get it in Launchpad and Finder:
        ln -sf "#{opt_prefix}/Tiny Hat Writer.app" /Applications/
    TEXT
  end

  test do
    assert_match "tinyhat-write", shell_output("#{bin}/tinyhat-write --version")
  end
end
EOF
( cd "$out" && sha256sum "tinyhat32-$ver.img.gz" "tinyhat-$ver.exe" "tinyhat-$ver-linux-x86_64" \
	"tinyhat-write-$ver.tar.gz" tinyhat-writer.rb >SHA256SUMS )
gpg --batch --yes --armor --detach-sign ${GPG_KEY:+-u "$GPG_KEY"} -o "$out/SHA256SUMS.asc" "$out/SHA256SUMS"
ls -l "$gz" "$out/tinyhat-$ver.exe" "$out/tinyhat-$ver-linux-x86_64" "$out/tinyhat-write-$ver.tar.gz" "$out/tinyhat-writer.rb" "$out/SHA256SUMS" "$out/SHA256SUMS.asc"
