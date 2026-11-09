return {
  name = "hiredis",
  version = "1.4.1",
  release = "1",
  desc = "Minimalistic C client library for Redis",
  url = "https://github.com/redis/hiredis/",
  license = { "BSD-3-Clause" },
  arch = { "i686", "x32" },
  source = { "https://github.com/redis/hiredis//archive/v${version}/hiredis-${version}.tar.gz" },
  b2 = {
    "78e142a9c9bad04f8c921ac0be7bcd35aac5508a97f6eee133a0ed78aa76901f65d737145a7d0c56d5b91c8faef263bae4ff79d846e4690ff93fea1efc171762",
  },
  depends = { "glibc" },
  build_dir = "hiredis-${version}",
  style = "make",
  make_args = { "PREFIX=/usr" },
  install_license = { "COPYING" },
}
