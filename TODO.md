## Plans

- [ ] When booting straight into Orbiton, fix the mouse when running the RayLib example under VirtualBox.
- [ ] Add an ASCII art drawing program?
- [ ] "sudo pacman -S kitty" and "sudo pacman -S firefox" should work after booting tinyhat32.
      If the user does this, then gtk3 + x + all sort of sillyness should just be dragged along and installed,
      replacing the Tiny Hat packages. Perhaps tinyhat needs to use the "cairo-wayland" technique for more
      packages, to achieve this. Also goes for wine, lutris etc.
- [ ] "sudo pacman -S lutris" should also work, and there should be a menu entry to installing+launcing it.
- [ ] Installing and launching "nethack" does not work. How come?
- [ ] Make "import turtle" work in Python, with a custom turtle module that uses SDL3 instead of Tcl/Tk.

### Don't do these yet

- [ ] fastfetch should have a Tiny Hat logo. Draw it in ASCII, then create a pull request.
- [ ] Enable 3D accelleration by default for the ova, but also check that Kitty + GLFW etc works.
- [ ] Verify the source checksums of all PKGBUILDs and TINYBUILDs before a build starts.
- [ ] Cross-build fuzzel and SDL3 for i686 against our own sysroot, and compare with today's builds.
- [ ] Build everything in the images from source, then the sysroot libraries.
- [ ] Reduce archlinux32 to a one-time bootstrap, then remove it and the `build_planner.py` heuristics it needs.
- [ ] Set the keyboard layout first in QEMU tests, and check the window title width, Space in NetSurfy and the menu installers.
- [ ] Include Lutris from its GTK4 branch, with wlr-randr, cabextract, psmisc, Pillow and the Python request libraries.
- [ ] Start XWayland on demand for X11-only programs, without having it in the images.
- [ ] Let `pacman -S` of poppler-glib (evince) work without downgrading Tiny Hat's newer poppler, and make `sonamecheck.py` report only sonames that something depends on.
- [ ] Let cairo-wayland satisfy `cairo`, so prebuilt packages like Firefox install on tinyhat32.
- [ ] Stop `pacman -S python-*` from installing modules built for an older Python than 3.14.
- [ ] Test in QEMU that AppImages, pip in a venv and `go install` programs run.
- [ ] Install and start the 50 most installed Arch packages in QEMU, and fix or list what fails.
