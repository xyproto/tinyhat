return {
  name = "libmd",
  version = "1.3.0",
  release = "1",
  desc = "Message Digest functions from BSD systems",
  url = "https://www.hadrons.org/software/libmd/",
  license = { "BSD-3-Clause AND BSD-2-Clause AND ISC AND Beerware AND CC-PDM-1.0" },
  arch = { "i686", "x32" },
  source = { "https://libbsd.freedesktop.org/releases/libmd-${version}.tar.xz" },
  sha512 = {
    "bc09299855fe21030fd15fcbf3981105b7c2fd1bc84ad9737a54e593236509d2f5cbef97feb7ec59cd520fbb95793f78c627b120b65b48ba4967fce4a1b47d5a",
  },
  depends = { "glibc" },
  build_dir = "libmd-${version}",
  style = "configure",
  configure = { "--prefix=/usr" },
  install_license = { "COPYING" },
}
