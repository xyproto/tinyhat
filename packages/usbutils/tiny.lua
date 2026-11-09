return {
  name = "usbutils",
  version = "020",
  release = "1",
  desc = "A collection of USB tools to query connected USB devices",
  url = "https://git.kernel.org/pub/scm/linux/kernel/git/gregkh/usbutils.git/",
  license = { "GPL-2.0-only", "GPL-2.0-or-later", "GPL-3.0-only", "LGPL-2.1-or-later", "MIT" },
  arch = { "i686", "x32" },
  source = { "https://www.kernel.org/pub/linux/utils/usb/usbutils/usbutils-${version}.tar.xz" },
  sha512 = {
    "61234ddd4f45dca4b33eb9d0babf09d82a3ddb3f02e7eec2709487a126951de626ca82af20809e75410e7f06875a6a83ad42ee65959686c58e9a9d73d11c14af",
  },
  depends = { "glibc", "hwdata" },
  optdepends = { "coreutils: for lsusb.py usage", "python: for lsusb.py usage", "sh: for usb-devices" },
  makedepends = { "meson" },
  libdepends = { "libusb", "systemd" },
  build_dir = "usbutils-${version}",
  style = "meson",
  install = {
    { "../build/usbreset", "usr/bin/", mode = 755 },
    { "{NEWS,README.md}", "usr/share/doc/usbutils/" },
  },
  install_license = { "LICENSES/*.txt" },
}
