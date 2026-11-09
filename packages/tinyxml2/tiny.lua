return {
  name = "tinyxml2",
  version = "11.0.0",
  release = "2",
  desc = "Simple XML parser made for easy integration",
  url = "https://github.com/leethomason/tinyxml2",
  license = { "LicenseRef-zlib" },
  arch = { "i686", "x32" },
  source = { "tinyxml2::git+https://github.com/leethomason/tinyxml2#tag=${version}" },
  sha512 = {
    "f13b0fa564bce8c6cd7940c69e65472f23164d2815c4f81c6aefe6527e861468fe089fb9e86f93bdbd3b6007db63d2888508ce13ed840c51a3f72a323a20ec8b",
  },
  depends = { "glibc", "libgcc", "libstdc++" },
  makedepends = { "git", "cmake" },
  build_dir = "tinyxml2",
  style = "cmake",
  configure = { "-DCMAKE_INSTALL_PREFIX=/usr", "-Dtinyxml2_SHARED_LIBS=ON" },
  install_license = { "LICENSE.txt" },
}
