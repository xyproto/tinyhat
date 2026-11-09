return {
  name = "desktop-file-utils",
  version = "0.28",
  release = "1",
  desc = "Command line utilities for working with desktop entries",
  url = "https://www.freedesktop.org/wiki/Software/desktop-file-utils",
  license = { "GPL-2.0-or-later" },
  arch = { "i686", "x32" },
  source = {
    "git+https://gitlab.freedesktop.org/xdg/desktop-file-utils.git#tag=${version}",
    "update-desktop-database.hook",
  },
  b2 = {
    "de178fec8f5f22ea1b3f50473358cf7d4401e56cc9c27b603e9b58a04d937af3893a981a4ef3409bbb0b64320ff0e751f06f2db58e23b4375011d608a5d298d3",
    "2420bfc3deea04a7a00da36043408fb6c6a89d626a52dd47fef165c6aedf0c9c332b87ee3c497e1093f6e4972284614eb6e5e59c1a9d3229acfd755a6285037a",
  },
  depends = { "glib2", "glibc" },
  makedepends = { "git", "meson" },
  build_dir = "desktop-file-utils",
  style = "meson",
  install = {
    { "../*.hook", "usr/share/libalpm/hooks/" },
  },
}
