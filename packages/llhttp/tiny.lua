return {
  name = "llhttp",
  version = "9.3.1",
  release = "1",
  desc = "Port of http_parser to llparse",
  url = "https://llhttp.org",
  license = { "MIT" },
  arch = { "i686", "x32" },
  source = {
    "llhttp-release-v${version}.tar.gz::https://github.com/nodejs/llhttp/archive/refs/tags/release/v${version}.tar.gz",
  },
  sha256 = { "c14a93f287d3dbd6580d08af968294f8bcc61e1e1e3c34301549d00f3cf09365" },
  depends = { "glibc" },
  makedepends = { "cmake" },
  build_dir = "llhttp-release-v${version}",
  style = "cmake",
  configure = { "-DBUILD_SHARED_LIBS=ON", "-DCMAKE_INSTALL_PREFIX=/usr" },
  install_license = { "LICENSE" },
}
