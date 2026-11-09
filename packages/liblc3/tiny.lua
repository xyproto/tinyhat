return {
  name = "liblc3",
  version = "1.1.3",
  release = "2",
  desc = "Low Complexity Communication Codec library and tools",
  url = "https://github.com/google/liblc3",
  license = { "Apache-2.0" },
  arch = { "i686", "x32" },
  source = { "git+https://github.com/google/liblc3#tag=v${version}" },
  b2 = {
    "5cd325138c59baa56317e6d0ccf04003743202b915e5efe7f9284153150f7bc5158e1a432eb0366c6640e2964b723b1ff28e8735da7cdb9c102870debc413cd3",
  },
  depends = { "glibc" },
  optdepends = { "python: Python bindings" },
  makedepends = { "git", "meson", "python" },
  provides = { "liblc3.so" },
  build_dir = "liblc3",
  style = "meson",
  configure = { "-Dpython=true", "-Dtools=true" },
  install = {
    { "README.md", "usr/share/doc/liblc3/" },
  },
}
