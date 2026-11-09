return {
  name = "tty-solitaire",
  version = "1.4.1",
  release = "1",
  desc = "ncurses-based klondike solitaire game",
  url = "https://github.com/mpereira/tty-solitaire",
  license = { "MIT" },
  arch = { "i686", "x32" },
  source = { "git+https://github.com/mpereira/tty-solitaire.git#tag=v${version}" },
  b2 = {
    "e9867c7a3895daa7e1756e10b60eb9426783ef1385c4ae99faf279a3795357a910df9a7b01492b0de544e58845b3ed256dbe7e5ac1626f95a7c860b49e48c55b",
  },
  depends = { "git", "ncurses" },
  build_dir = "tty-solitaire",
  style = "make",
  install_license = { "LICENSE" },
}
