return {
  name = "mtdev",
  version = "1.1.7",
  release = "1",
  desc = "A stand-alone library which transforms all variants of kernel MT events to the slotted type B protocol",
  url = "https://bitmath.org/code/mtdev/",
  license = { "MIT" },
  arch = { "i686", "x32" },
  source = { "https://bitmath.org/code/mtdev/mtdev-${version}.tar.bz2" },
  sha512 = {
    "e6174a38cf67a7f12a3b91e4e27bf74a18d6b40a956950ebb748b0ff87092333daa07e647b26038a5a533f8c48e845d649848e6ba99ea009ab87fd96ed188152",
  },
  depends = { "glibc" },
  build_dir = "mtdev-${version}",
  style = "configure",
  configure = { "--prefix=/usr", "--disable-static" },
  install_license = { "COPYING" },
}
