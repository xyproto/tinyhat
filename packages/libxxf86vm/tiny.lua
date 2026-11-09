return {
  name = "libxxf86vm",
  version = "1.1.7",
  release = "1",
  desc = "X11 XFree86 video mode extension library",
  url = "https://xorg.freedesktop.org/",
  license = { "X11-distribute-modifications-variant" },
  arch = { "i686", "x32" },
  source = { "https://xorg.freedesktop.org//releases/individual/lib/libXxf86vm-${version}.tar.xz" },
  sha512 = {
    "d1051c9698a884d86e5beb00d5ee148d2b5ded7fd05168861f722b89643ad9b7f7d220f0cbb64b290a69faf9a6630181533aaddb01c9c68b46f1e5625030f094",
  },
  depends = { "libxext", "libx11", "glibc", "xorgproto" },
  build_dir = "libXxf86vm-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--disable-static" },
  install_license = { "COPYING" },
}
