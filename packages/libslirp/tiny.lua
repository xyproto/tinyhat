return {
  name = "libslirp",
  version = "4.9.5",
  release = "1",
  desc = "General purpose TCP-IP emulator",
  url = "https://gitlab.freedesktop.org/slirp/libslirp",
  license = { "BSD", "MIT" },
  arch = { "i686", "x32" },
  source = {
    "https://gitlab.freedesktop.org/slirp/libslirp/-/archive/v${version}/libslirp-v${version}.tar.gz",
  },
  sha512 = {
    "b8ed4209f8d8f64908299e8c43985dc3dc1c42c980721f7897b8a887e7d90c15e961f4f2e3273e3e5fd8d10d3ae33b8643a9f0e094dd2afd2abc4fa129a896d6",
  },
  depends = { "glib2" },
  makedepends = { "meson" },
  provides = { "libslirp.so" },
  build_dir = "libslirp-v${version}",
  style = "meson",
  install = {
    { "COPYRIGHT", "usr/share/licenses/libslirp/LICENSE" },
  },
}
