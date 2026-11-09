return {
  name = "which",
  version = "2.25",
  release = "1",
  desc = "A utility to show the full path of commands",
  url = "https://savannah.gnu.org/projects/which/",
  license = { "GPL-3.0-or-later" },
  arch = { "i686", "x32" },
  source = { "https://ftp.gnu.org/gnu/which/which-${version}.tar.gz" },
  b2 = {
    "9c9b0e197e0f35845fee8a050aeccb3407ea27f82955be028ee3f148d74bf5d2c844814657ccf16adf03176f94b40bb4ae8817f27ffc7b2196b8d2e1e77f82c8",
  },
  depends = { "bash", "glibc" },
  build_dir = "which-${version}",
  style = "configure",
  configure = { "--prefix=/usr" },
}
