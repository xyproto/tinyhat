return {
  name = "libebur128",
  version = "1.2.6",
  release = "2",
  desc = "A library that implements the EBU R 128 standard for loudness normalisation",
  url = "https://github.com/jiixyj/libebur128",
  license = { "MIT" },
  arch = { "i686", "x32" },
  source = { "git+https://github.com/jiixyj/libebur128#tag=v${version}" },
  sha256 = { "ee74f29f3f4d339f71de831af341b36d2cf24d0129e5bec0512b145d66da3daf" },
  depends = { "glibc" },
  makedepends = { "cmake", "git" },
  build_dir = "libebur128",
  style = "cmake",
  configure = { "-DCMAKE_INSTALL_PREFIX=/usr", "-DBUILD_STATIC_LIBS=OFF" },
  install = {
    { "COPYING", "usr/share/licenses/libebur128/LICENSE" },
  },
}
