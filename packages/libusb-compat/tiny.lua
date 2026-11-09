return {
  name = "libusb-compat",
  version = "0.1.9",
  release = "1",
  desc = "Library to enable user space application programs to communicate with USB devices",
  url = "http://libusb.sourceforge.net/",
  license = { "LGPL-2.0-or-later" },
  arch = { "i686", "x32" },
  source = {
    "https://github.com/libusb/libusb-compat-0.1/releases/download/v${version}/libusb-compat-${version}.tar.gz",
  },
  sha256 = { "b09aa33e943080352041ea3d5ac01fd90ea96e9337212865de0dda776886dbb9" },
  depends = { "libusb", "sh" },
  build_dir = "libusb-compat-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--disable-static" },
}
