return {
  name = "libyaml",
  version = "0.2.5",
  release = "3",
  desc = "YAML 1.1 library",
  url = "https://pyyaml.org/wiki/LibYAML",
  license = { "MIT" },
  arch = { "i686", "x32" },
  source = { "https://pyyaml.org/download/libyaml/yaml-${version}.tar.gz" },
  sha512 = {
    "dadd7d8e0d88b5ebab005e5d521d56d541580198aa497370966b98c904586e642a1cd4f3881094eb57624f218d50db77417bbfd0ffdce50340f011e35e8c4c02",
  },
  depends = { "glibc" },
  build_dir = "yaml-${version}",
  style = "configure",
  configure = { "--prefix=/usr" },
  install = {
    { "License", "usr/share/licenses/libyaml/LICENSE" },
  },
}
