return {
  name = "libraqm",
  version = "0.11.0",
  release = "1",
  desc = "A library that encapsulates the logic for complex text layout",
  url = "https://github.com/HOST-Oman/libraqm/",
  license = { "MIT" },
  arch = { "i686", "x32" },
  source = { "git+https://github.com/HOST-Oman/libraqm#tag=v${version}" },
  sha256 = { "6f3ddc83bc6de239fc992ce2ed8f69397813ca7977c7067c6d20e822122c6782" },
  depends = { "freetype2", "fribidi", "glibc", "harfbuzz" },
  makedepends = { "git", "gtk-doc", "meson" },
  build_dir = "libraqm",
  style = "meson",
  configure = { "--auto-features=auto", "--prefix=/usr", "-Ddocs=true" },
  install = {
    { "COPYING", "usr/share/licenses/libraqm/LICENSE" },
  },
}
