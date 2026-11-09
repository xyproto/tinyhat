return {
  name = "chibi-scheme",
  version = "0.12",
  release = "1",
  desc = "Minimal Scheme implementation for use as an extension language",
  url = "https://github.com/ashinn/chibi-scheme",
  license = { "BSD-3-Clause" },
  arch = { "i686", "x32" },
  source = {
    "chibi-scheme-${version}.tar.gz::https://github.com/ashinn/chibi-scheme/archive/refs/tags/${version}.tar.gz",
  },
  sha256 = { "b70a1147bc70a0f90df3fb6081bc99808237fd17a9accf9ee7a2cc20d95a4df0" },
  depends = { "glibc" },
  build_dir = "chibi-scheme-${version}",
  style = "make",
  make_args = { "PREFIX=/usr" },
  install_license = { "COPYING" },
}
