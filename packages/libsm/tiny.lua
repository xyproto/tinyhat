return {
  name = "libsm",
  version = "1.2.6",
  release = "1",
  desc = "X11 Session Management library",
  url = "https://xorg.freedesktop.org/",
  license = { "MIT", "MIT-open-group" },
  arch = { "i686", "x32" },
  source = { "https://xorg.freedesktop.org//releases/individual/lib/libSM-${version}.tar.xz" },
  sha512 = {
    "e544a1dc49a03390f76af35837bfd01f749b806d88d3ee806f20311c47ff53d0aeea4744feb875958031b17d50b57a89dcc41d81241c09333c88b268c44653a7",
  },
  depends = { "libice", "util-linux-libs", "xorgproto", "glibc" },
  libdepends = { "xorg-util-macros", "xtrans" },
  build_dir = "libSM-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--sysconfdir=/etc" },
  install_license = { "COPYING" },
}
