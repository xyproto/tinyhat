return {
  name = "hwdata",
  version = "0.412",
  release = "1",
  desc = "hardware identification databases",
  url = "https://github.com/vcrhonek/hwdata",
  license = { "GPL-2.0-or-later" },
  arch = { "any" },
  source = { "git+https://github.com/vcrhonek/hwdata.git#tag=v${version}?signed" },
  b2 = {
    "8f5edaa72fd229b37acb4f5af3805e6a25606bef496742b05afee0a358871735854ac8cfdf44852dd33d0162afca2b887a24e9d4e0695e28f23e8f04204f32ae",
  },
  makedepends = { "git" },
  replaces = { "hwids" },
  build_dir = "hwdata",
  style = "configure",
  configure = { "--prefix=/usr", "--disable-blacklist" },
}
