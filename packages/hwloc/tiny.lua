return {
  name = "hwloc",
  version = "2.15.0",
  release = "1",
  desc = "Portable Hardware Locality is a portable abstraction of hierarchical architectures",
  url = "https://www.open-mpi.org/projects/hwloc/",
  license = { "BSD-3-Clause" },
  arch = { "i686", "x32" },
  source = { "https://www.open-mpi.org/software/hwloc/v2.15/downloads/hwloc-${version}.tar.bz2" },
  sha512 = {
    "3eca90872686c5887c6430b9d0b1e7ad50942f2afe77d599a79677c55072183809ca3cef2a68f727e8fa12183b4d2da0a5dea06aee4a9ea416059d3df3e18126",
  },
  depends = { "glibc", "libpciaccess", "ncurses", "systemd-libs" },
  optdepends = { "cairo: PDF, Postscript, and PNG export support", "libxml2: full XML import/export support" },
  libdepends = { "libx11", "libxml2", "systemd" },
  options = { "!docs" },
  arch_overrides = {
    i686 = {
      libdepends = { "libx11", "libxml2", "systemd", "cairo" },
    },
    pentium4 = {
      libdepends = { "libx11", "libxml2", "systemd", "cairo" },
    },
  },
  build_dir = "hwloc-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--sbindir=/usr/bin", "--enable-plugins", "--sysconfdir=/etc" },
  install_license = { "COPYING" },
}
