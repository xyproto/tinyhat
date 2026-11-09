return {
  name = "libice",
  version = "1.1.2",
  release = "1",
  desc = "X11 Inter-Client Exchange library",
  url = "https://xorg.freedesktop.org/",
  license = { "MIT-open-group" },
  arch = { "i686", "x32" },
  source = { "https://xorg.freedesktop.org//releases/individual/lib/libICE-${version}.tar.xz" },
  sha512 = {
    "340f51ffa1f14ed442ab8bcea92dd63df147c48242e232e818cafe02f43de7ab6e99c5430b9cb8d0dc661295239d2b3f6bdb6a092ce51a98afa06235257e9b1f",
  },
  depends = { "glibc", "xorgproto" },
  libdepends = { "xtrans", "xorg-util-macros" },
  build_dir = "libICE-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--sysconfdir=/etc" },
  install_license = { "COPYING" },
}
