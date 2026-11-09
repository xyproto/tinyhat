return {
  name = "libidn2",
  version = "2.3.8",
  release = "1",
  desc = "Free software implementation of IDNA2008, Punycode and TR46",
  url = "https://www.gnu.org/software/libidn/#libidn2",
  license = { "GPL2", "LGPL3" },
  arch = { "i686", "x32" },
  source = { "https://ftp.gnu.org/gnu/libidn/libidn2-${version}.tar.gz" },
  sha256 = { "f557911bf6171621e1f72ff35f5b1825bb35b52ed45325dcdee931e5d3c0787a" },
  depends = { "libunistring" },
  provides = { "libidn2.so" },
  arch_overrides = {
    i686 = {
      libdepends = { "ruby-ronn-ng" },
    },
    pentium4 = {
      libdepends = { "ruby-ronn-ng" },
    },
  },
  build_dir = "libidn2-${version}",
  style = "configure",
  configure = { "--disable-dependency-tracking", "--prefix=/usr", "--disable-static" },
}
