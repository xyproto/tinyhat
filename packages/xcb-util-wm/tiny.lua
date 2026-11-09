return {
  name = "xcb-util-wm",
  version = "0.4.2",
  release = "2",
  desc = "Utility libraries for XC Binding - client and window-manager helpers for ICCCM",
  url = "https://xcb.freedesktop.org",
  license = { "X11-distribute-modifications-variant" },
  arch = { "i686", "x32" },
  source = { "https://xorg.freedesktop.org/archive/individual/lib/xcb-util-wm-${version}.tar.xz" },
  sha512 = {
    "eab6588e6a47d28a3b25ecd13b5018529419db6b49c74458c093699b60a194933be1e1f6f7799abf03344f7ebc7af88586ff2935938d53644bdd5c763a7fa9b0",
  },
  depends = { "libxcb", "glibc" },
  libdepends = { "xorg-util-macros", "xorgproto" },
  build_dir = "xcb-util-wm-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--disable-static" },
  install = {
    { "COPYING", "usr/share/licenses/xcb-util-wm/LICENSE" },
  },
}
