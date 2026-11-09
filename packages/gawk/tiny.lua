return {
  name = "gawk",
  version = "5.4.1",
  release = "1",
  desc = "GNU version of awk",
  url = "https://www.gnu.org/software/gawk/",
  license = { "GPL-3.0-or-later" },
  arch = { "i686", "x32" },
  source = { "https://ftp.gnu.org/pub/gnu/gawk/gawk-${version}.tar.gz" },
  b2 = {
    "657cc8fcf0e51e2637b456a209c676a05d37f2740522e3fde8b7ff04fbb98019baed2b353269f822ee638e45a2770b9fa8fd3f66f95dd2e63c4e41e90c9dd682",
  },
  depends = { "sh", "glibc", "mpfr" },
  provides = { "awk" },
  build_dir = "gawk-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--libexecdir=/usr/lib", "--sysconfdir=/etc", "--without-libsigsegv" },
}
