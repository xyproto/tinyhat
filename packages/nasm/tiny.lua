return {
  name = "nasm",
  version = "3.02",
  release = "1",
  desc = "80x86 assembler designed for portability and modularity",
  url = "https://www.nasm.us",
  license = { "BSD-2-Clause" },
  arch = { "i686", "x32" },
  source = { "https://www.nasm.us/pub/nasm/releasebuilds/${version}/nasm-${version}.tar.xz" },
  sha512 = {
    "dc80d8a9a582423e62703da3cc3f37ee57735939b975faa8a72d061a8b596f763d206c7cc3e48c32d2ad726f38e430dc3b85cffd0c3b32e71e20ad9cc24f4804",
  },
  depends = { "glibc", "zlib" },
  makedepends = { "perl-font-ttf", "perl-sort-versions", "xmlto", "asciidoc" },
  libdepends = { "fontconfig", "ttf-roboto", "ttf-roboto-mono", "ghostscript", "diffutils" },
  arch_overrides = {
    i686 = {
      libdepends = { "fontconfig", "ttf-roboto", "ttf-roboto-mono", "ghostscript", "diffutils", "ttf-liberation" },
    },
    pentium4 = {
      libdepends = { "fontconfig", "ttf-roboto", "ttf-roboto-mono", "ghostscript", "diffutils", "ttf-liberation" },
    },
  },
  build_dir = "nasm-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--docdir=/usr/share/doc/nasm", "--htmldir=/usr/share/doc/nasm/html" },
  make_args = { "all", "doc" },
  install_args = { "install_doc" },
  install_license = { "LICENSE" },
}
