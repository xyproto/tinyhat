return {
  name = "dosfstools",
  version = "4.2",
  release = "5",
  desc = "DOS filesystem utilities",
  url = "https://github.com/dosfstools/dosfstools",
  license = { "GPL-3.0-or-later" },
  arch = { "i686", "x32" },
  source = {
    "https://github.com/dosfstools/dosfstools/releases/download/v${version}/dosfstools-${version}.tar.gz",
    "dosfstools-reproducible-mkfs.patch::https://github.com/dosfstools/dosfstools/commit/8da7bc93315cb0c32ad868f17808468b81fa76ec.patch",
  },
  sha256 = {
    "64926eebf90092dca21b14259a5301b7b98e7b1943e8a201c7d726084809b527",
    "b9953dde3d755a6e9a31a28e2a7aebccd0b5ed9bcc664893bd55290945589b5b",
  },
  depends = { "glibc" },
  build_dir = "dosfstools-${version}",
  patches = { "dosfstools-reproducible-mkfs.patch" },
  style = "configure",
  configure = {
    "--prefix=/usr",
    "--libexecdir=/usr/lib",
    "--sbindir=/usr/bin",
    "--mandir=/usr/share/man",
    "--docdir=/usr/share/doc/dosfstools",
    "--enable-compat-symlinks",
  },
}
