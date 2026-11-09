return {
  name = "glib-networking",
  version = "2.90.0",
  release = "1",
  epoch = "1",
  desc = "Network extensions for GLib",
  url = "https://gitlab.gnome.org/GNOME/glib-networking",
  license = { "LGPL-2.1-or-later" },
  arch = { "i686", "x32" },
  source = { "git+https://gitlab.gnome.org/GNOME/glib-networking.git#tag=${version}" },
  b2 = {
    "ac86ab856da1a07e074c304cf6c3c8ae9b09bcf0faf360d1f21f6ea0f7b421476f97e379e517a19804a3eb1ac08321526b4f47faa9b6299658d800fd75cfee81",
  },
  depends = { "glib2", "glibc", "gnutls", "gsettings-system-schemas", "libproxy" },
  makedepends = { "git", "meson" },
  libdepends = { "gsettings-desktop-schemas" },
  build_dir = "glib-networking",
  style = "meson",
}
