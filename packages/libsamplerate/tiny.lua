return {
  name = "libsamplerate",
  version = "0.2.2",
  release = "3",
  desc = "An audio sample rate conversion library",
  url = "https://libsndfile.github.io/libsamplerate/",
  license = { "BSD" },
  arch = { "i686", "x32" },
  source = {
    "https://github.com/libsndfile/libsamplerate/releases/download/${version}/libsamplerate-${version}.tar.xz",
  },
  sha512 = {
    "d23ae54d23209ba22baae9e5fd178dd8e0e99205dada7e7c3a7b3a3d8cf816ed427a411bfeb008427f64da7767d645edce40811f238af11c8c386f5ef25a9f0c",
  },
  depends = { "glibc" },
  makedepends = { "cmake" },
  libdepends = { "alsa-lib", "libsndfile", "opus" },
  provides = { "libsamplerate.so" },
  build_dir = "libsamplerate-${version}",
  style = "cmake",
  configure = { "-DCMAKE_INSTALL_PREFIX=/usr", "-DCMAKE_BUILD_TYPE=None", "-DBUILD_SHARED_LIBS=ON", "-Wno-dev" },
  install = {
    { "{AUTHORS,NEWS,README.md,ChangeLog}", "usr/share/doc/libsamplerate/" },
  },
  install_license = { "COPYING" },
}
