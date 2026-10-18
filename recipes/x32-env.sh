host=x86_64-linux-gnux32
export PATH=/opt/x32/bin:$PATH
export CC=$host-gcc CXX=$host-g++ AR=$host-ar RANLIB=$host-ranlib NM=$host-nm STRIP=$host-strip OBJCOPY=$host-objcopy
export CFLAGS="-O2 -pipe" CXXFLAGS="-O2 -pipe" LDFLAGS="-Wl,-O1,--as-needed"
export PKG_CONFIG_LIBDIR=/opt/x32/usr/libx32/pkgconfig:/opt/x32/usr/share/pkgconfig PKG_CONFIG_SYSROOT_DIR=/opt/x32
unset PKG_CONFIG_PATH
cross=/work/x32-cross.ini
cat >"$cross" <<CROSS
[binaries]
c = '$host-gcc'
cpp = '$host-g++'
ar = '$host-ar'
strip = '$host-strip'
pkg-config = 'pkg-config'

[properties]
sys_root = '/opt/x32'
pkg_config_libdir = '$PKG_CONFIG_LIBDIR'

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
CROSS

x32_finish() {
	out=/out/x32-$1
	cp -a "$out/." /opt/x32/
	find /opt/x32/usr/libx32 -name '*.la' -delete
	rm -rf "$out/usr/include" "$out/usr/share" "$out/usr/bin" "$out/usr/libx32/pkgconfig" "$out/usr/lib"
	find "$out" \( -name '*.a' -o -name '*.la' \) -delete
	find "$out" -type f -name '*.so*' -exec "$STRIP" --strip-unneeded {} +
}
