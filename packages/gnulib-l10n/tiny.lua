return {
  name = "gnulib-l10n",
  version = "20241231",
  release = "1",
  desc = "The Gnulib localizations",
  url = "https://www.gnu.org/software/gnulib/",
  license = { "LGPL-2.0-or-later" },
  arch = { "any" },
  source = { "https://ftp.gnu.org/pub/gnu/gnulib/gnulib-l10n-${version}.tar.gz" },
  b2 = {
    "93283b255416aa7d041afb2275fc3a47bfe1d46a41987af5d38ad98f6962b0008ce88ffd35f284d813a2d99b891c470489f05d3ecc1bc182ce9912d82a5d0669",
  },
  build_dir = "gnulib-l10n-${version}",
  style = "configure",
  configure = { "--prefix=/usr" },
}
