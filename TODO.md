## Plans

### Yes

- [ ] Reduce archlinux32 to a one-time bootstrap, then remove it and the `build_planner.py` heuristics it needs.
- [ ] Let cairo-wayland satisfy `cairo`, so prebuilt packages like Firefox install on tinyhat32.
- [ ] Stop `pacman -S python-*` from installing modules built for an older Python than 3.14.
- [ ] python-pyxel with thin LTO on i586 without SSE fails with "Do not know how to split this operator's operand!"; the TINYBUILD sets CARGO_PROFILE_RELEASE_LTO=false. Reduce it.

### Yes, but later

- [ ] Version checking in the package files: patch nvchecker to read its settings from TINYBUILD and PKGBUILD files, not only .nvchecker.toml; then move the nvchecker settings out of distro.toml/distro.lock into the TINYBUILDs, and let autoupdate.py and makep --sup use them.

### Maybe

- [ ] Add an ASCII art drawing program?
- [ ] LLVM: libLTO.so and libRemarks.so still contain /startdir/ (the build path), so llvm has "options leakpath" for now.
      libclc has it too: its libclc.a bitcode archives contain the build path.
      Find where it comes from (readelf -d, strings) and remove it, then drop "options leakpath".
- [ ] Verify the source checksums of all PKGBUILDs and TINYBUILDs before a build starts.
- [ ] Cross-build fuzzel and SDL3 for i686 against our own sysroot, and compare with today's builds.
- [ ] Build everything in the images from source, then the sysroot libraries.
- [ ] Set the keyboard layout first in QEMU tests, and check the window title width, Space in TinySurf and the menu installers.
- [ ] Start XWayland on demand for X11-only programs, without having it in the images.
- [ ] Let `pacman -S` of poppler-glib (evince) work without downgrading Tiny Hat's newer poppler, and make `sonamecheck.py` report only sonames that something depends on.

### Possibly never

- [ ] Include Lutris from its GTK4 branch, with wlr-randr, cabextract, psmisc, Pillow and the Python request libraries.

### To test

- [ ] "sudo pacman -S kitty" and "sudo pacman -S firefox" should work after booting tinyhat32.
      If the user does this, then gtk3 + x + all sort of sillyness should just be dragged along and installed,
      replacing the Tiny Hat packages. Perhaps tinyhat needs to use the "cairo-wayland" technique for more
      packages, to achieve this. Also goes for wine, lutris etc.
- [ ] "sudo pacman -S lutris" should also work, and there should be a menu entry to installing+launcing it.
- [ ] Installing and launching "nethack" does not work. How come?
- [ ] Make "import turtle" work in Python, with a custom turtle module that uses SDL3 instead of Tcl/Tk.
- [ ] Enable 3D accelleration by default for the ova, but also check that Kitty + GLFW etc works.
