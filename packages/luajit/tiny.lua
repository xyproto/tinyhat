return {
  name = "luajit",
  version = "2.1.1792349434+dd3b243",
  release = "1",
  desc = "Just-in-time compiler and drop-in replacement for Lua 5.1",
  url = "https://luajit.org/",
  license = { "MIT" },
  arch = { "i686" },
  source = {
    "LuaJIT-dd3b243.tar.gz::https://github.com/LuaJIT/LuaJIT/archive/dd3b243.tar.gz",
    "c7815e1a1b49871e645252bb12e722fb4879df11.patch",
  },
  b2 = {
    "d7d6b358668be45e50ee139f8e244a2625cbcd63f95677d4709e4f38a4753181edeff764a53421dc741f00787e4385d315a78dd3cd8852e2f19c7595d8cd62db",
    "fabd0be78309d8211e96d4d0cf105ae6a27f46110d8b19513d10bdd7c6045221fc5c8e21d1bcc1def854d7d1bf6473c99fb7bc7e4d5307188129c3c87cf8c69b",
  },
  depends = { "glibc", "libgcc" },
  build_dir = "LuaJIT-dd3b243*",
  patches = { "c7815e1a1b49871e645252bb12e722fb4879df11.patch" },
  style = "make",
  make_args = { "amalg", "PREFIX=/usr", "BUILDMODE=dynamic", "TARGET_STRIP= @:" },
  install_license = { "COPYRIGHT" },
}
