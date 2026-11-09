return {
  name = "libxcursor",
  version = "1.2.3",
  release = "1",
  desc = "X cursor management library",
  url = "https://gitlab.freedesktop.org/xorg/lib/libxcursor",
  license = { "HPND-sell-variant" },
  arch = { "i686", "x32" },
  source = { "https://xorg.freedesktop.org//releases/individual/lib/libXcursor-${version}.tar.xz" },
  sha512 = {
    "069a1eb27a0ee1b29b251bb6c2d0688543a791d6862fad643279e86736e1c12ca6fc02b85b8611c225a9735dc00efab84672d42b547baa97304362f0c5ae0b5a",
  },
  depends = { "libxfixes", "libxrender", "default-cursors", "libx11", "glibc", "xorgproto" },
  libdepends = { "xorg-util-macros" },
  build_dir = "libXcursor-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--sysconfdir=/etc", "--disable-static" },
  install_license = { "COPYING" },
}
