#!/bin/sh
set -eu
umask 022
top=$(dirname "$(dirname "$(readlink -f "$0")")")
ver=$1
img=$2
home=${DIST_HOME:-https://github.com/xyproto/tinyhat}
base=${DIST_URL:-$home/releases/download}
out=$top/build/dist
stage=$out/src/tinyhat-write-$ver
mkdir -p "$out"
if [ ! -f "$out/img.gz" ] || [ "$img" -nt "$out/img.gz" ]; then
	gzip -c -9 "$img" >"$out/img.gz.tmp"
	mv "$out/img.gz.tmp" "$out/img.gz"
fi
x86_64-w64-mingw32-windres "$top/programs/tinyhat-write-win32.rc" -o "$out/win-res.o"
cd "$out"
x86_64-w64-mingw32-gcc -Os -s -static -mwindows -DEMBED_IMG -DTW_VERSION="\"$ver\"" \
	-o "$out/tinyhat-$ver.exe" "$top/programs/tinyhat-write-win32.c" "$out/win-res.o" -lcomctl32 -lshell32
rm -f "$out/win-res.o"
cc -Os -s -static-libgcc -DEMBED_IMG -DTW_VERSION="\"$ver\"" \
	-o "$out/tinyhat-$ver-linux-x86_64" "$top/programs/tinyhat-write.c" \
	$(pkg-config --cflags --libs gtk+-3.0)
rm -rf "$out/src"
mkdir -p "$stage"
cp "$top/programs/tinyhat-write.c" "$top/programs/tinyhat-write-core.h" "$stage/"
tar -C "$out/src" -czf "$out/tinyhat-write-$ver.tar.gz" "tinyhat-write-$ver"
sha=$(sha256sum "$out/tinyhat-write-$ver.tar.gz" | awk '{ print $1 }')
cat >"$out/tinyhat-writer.rb" <<EOF
class TinyhatWriter < Formula
  desc "Write Tiny Hat images to USB sticks"
  homepage "$home"
  url "$base/v$ver/tinyhat-write-$ver.tar.gz"
  sha256 "$sha"

  depends_on "pkgconf" => :build
  depends_on "gtk+3"

  def install
    system ENV.cc, "-O2", "-DTW_VERSION=\\"$ver\\"", "tinyhat-write.c", "-o", "tinyhat-write", *shell_output("pkg-config --cflags --libs gtk+-3.0").split
    bin.install "tinyhat-write"
  end

  test do
    assert_match "tinyhat-write", shell_output("#{bin}/tinyhat-write --version")
  end
end
EOF
ls -l "$out/tinyhat-$ver.exe" "$out/tinyhat-$ver-linux-x86_64" "$out/tinyhat-write-$ver.tar.gz" "$out/tinyhat-writer.rb"
