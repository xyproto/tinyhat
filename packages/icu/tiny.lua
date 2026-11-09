return {
  name = "icu",
  version = "78.3",
  release = "1",
  desc = "International Components for Unicode library",
  url = "https://icu.unicode.org",
  license = { "LicenseRef-Unicode-3.0", "BSD-2-Clause", "BSD-3-Clause", "NAIST-2003" },
  arch = { "i686", "x32" },
  source = {
    "https://github.com/unicode-org/icu/releases/download/release-${version}/icu4c-${version}-sources.tgz",
    "ICU-22132.patch",
  },
  sha512 = {
    "04a49455e1489030c520a4bfd2664fa2171e7938d08f2acdbbcb1fda976639fd8b1f0704f2eec89ba59a7b6d118ceaab6ec5a096e40d9085a0895d91ce225245",
    "1178062ccfcf7ecc698c64132b3612e73f9c4b0bbfaa668ae2039f3eb4cb2722d0b08a9f45b057da10def7a308d5c8d14c0c644892e7f11092c9cc488c850ab7",
  },
  depends = { "glibc", "libstdc++", "libgcc", "sh" },
  makedepends = { "python" },
  provides = { "libicudata.so", "libicui18n.so", "libicuio.so", "libicutest.so", "libicutu.so", "libicuuc.so" },
  build_dir = "icu/source",
  style = "configure",
  configure = { "--prefix=/usr", "--sysconfdir=/etc", "--mandir=/usr/share/man", "--sbindir=/usr/bin" },
  install_license = { "../LICENSE" },
}
