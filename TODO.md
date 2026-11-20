## Plans

- [ ] When booting straight into Orbiton, fix the mouse when running the RayLib example under VirtualBox.
- [ ] When orbiton opens a file named "book*.md", the book mode should be used by default.
- [x] The window title looks excellent in dwl, but the window buttons behave weirdly.
      If a window title is shown, the button for that window should be hidden.
      If a window is shown, it should also be brought to the front.
      If the title or window button for the active window is clicked, it should be sent to the back
      (or hidden)?
- [ ] When 2x ctrl-space building+running C64 programs, there is too much stdout+stderr noise.
- [x] People love to draw squares with the left mouse button on the background.
      Draw squares the first three times, then draw triangles 2 times, then a circle, then flames.
- [ ] fastfetch should have a Tiny Hat logo. Draw it in ASCII, then create a pull request.
- [ ] Add an ASCII art drawing program?
- [x] The fan icon should either be a tiny fan, a normal fan or a normal fan in orange.
      There should be no text to the right of it.
      The fan icon should have mouse over text.
      When running in a VM, there should be no fan icon.
- [x] If system sounds for the fan are enabled, the fans should have different "wroom wroom" sounds (racer, scooter, windgust).
- [x] ctrl-alt-left and ctrl-alt-right should be possible to use for cycling CPU+fan modes.
      There should also be background help text about this (Ctrl+Alt+Left / Right   Fan/CPU mode).
- [ ] monkeyjumpgo should open its own help contents just by extending the window to the right.
      There should be a ">>" button for opening the help. The help should be opened by default the first time,
      but not after that. The status should appear in the bottom left corner, like in NetSurfy, but with
      a black background that is 20% transparent and with white text that is #e0e0e0.
- [ ] "sudo pacman -S kitty" and "sudo pacman -S firefox" should work after booting tinyhat32.
      If the user does this, then gtk3 + x + all sort of sillyness should just be dragged along and installed,
      replacing the Tiny Hat packages. Perhaps tinyhat needs to use the "cairo-wayland" technique for more
      packages, to achieve this. Also goes for wine, lutris etc.
- [ ] "sudo pacman -S lutris" should also work, and there should be a menu entry to installing+launcing it.
- [x] There should not be a duplicate NetSurfy menu icon in the alt-space menu.
- [ ] The address line should have focus when starting NetSurfy, and typing should clear it, like in many
      other browsers. And ctrl-l should work even if the keyboard focus is in a search box on a page, to
      bring focus to the address bar.
- [x] Bonzomatic has a larger window than what fits Tiny Hat when launched from Virtual Box (+3D enabled,
      need to test if this is also with 3D disabled).
- [ ] Installing and launching "nethack" does not work. How come?

### Unsure about these

- [ ] Enable 3D accelleration by default for the ova, but also check that Kitty + GLFW etc works.

### Unsorted, maybe completed

- [x] Install exactly the planned package versions in build sandboxes, not the newest file in `build/repo`.
- [ ] Verify the source checksums of all PKGBUILDs and TINYBUILDs before a build starts.
- [ ] Use fish instead of bash wherever possible: Tiny Hat's own scripts first, a small POSIX `/bin/sh`, then `pacman-key` and `ldd`. (Packages are TINYBUILDs, built by makep, since 2026-11-15.)
- [ ] Cross-build fuzzel, mpg123 and SDL3 for i686 against our own sysroot, and compare with today's builds.
- [ ] Build everything in the images from source, then the sysroot libraries.
- [ ] Reduce archlinux32 to a one-time bootstrap, then remove it and the `build_planner.py` heuristics it needs.
- [ ] Set up a signed Tiny Hat pacman repo for cnc-ddraw, JBR, CGoban and tomlc17.
- [ ] Release monkeyjump, tinyhat-bootsplash and tinyhat-play, then bump their TINYBUILDs.
- [ ] Set the keyboard layout first in QEMU tests, and check the window title width, Space in NetSurfy and the menu installers.
- [ ] Test in QEMU: FreeDOS, "Download and run a game...", Turbo Pascal, Pyxel, Orbiton boot mode and `startcheck`.
- [ ] Test on real hardware: the window title, Pyxel on the Radeon, NetSurfy focus and the Lutris install.
- [ ] Remove cairo's X11 backend, libxext, libxrender and anything else that pulls in X11 or GTK3.
- [ ] Apply the Wayland-first Wine and JBR installer changes, then test CGoban 3 and StarCraft.
- [ ] Include Lutris from its GTK4 branch, with wlr-randr, cabextract, psmisc, Pillow and the Python request libraries.
- [ ] Test the guru meditation crash screen on every path, including the QR code on a text console.
- [ ] Build the OVA with guest additions and 3D acceleration.
- [ ] Share one helper between the menu installers, with the pins in a data file that autoupdate checks.
- [x] Let Ctrl+Q in Orbiton boot mode play the shutdown animation and reboot.
- [x] Fix showing and hiding windows with the status bar buttons.
- [x] Make `ls` list plain names; keep the details for `ll`, `la` and `dir`.
- [x] Use `--color=auto` in the `ls`, `dir` and `tree` aliases, so `ls | grep` gets no escape codes.
- [x] Pass the arguments of the `history` wrapper on, so `history search`, `delete` and `clear` work.
- [x] Replace the `wget` alias with a function that handles `-O`, `-q`, `-c` and several URLs.
- [x] Make `untar` use `tar xvf`, so `.tar.xz` and `.tar.zst` work too.
- [x] Make `update` and `cleanup` use Tiny Hat's pacman wrapper, and `cleanup` quiet when there are no orphans.
- [ ] Give bash the same aliases and environment as fish, for users and programs that start bash.
- [x] Add a `sudo` that runs doas, for scripts and instructions that call sudo.
- [ ] Add sdl2-compat (SDL2 on SDL3), so SDL2 games and tools from pacman run.
- [ ] Start XWayland on demand for X11-only programs, without having it in the images.
- [ ] Let `pacman -S` of poppler-glib (evince) work without downgrading Tiny Hat's newer poppler, and make `sonamecheck.py` report only sonames that something depends on.
- [ ] Let cairo-wayland satisfy `cairo`, so prebuilt packages like Firefox install on tinyhat32.
- [ ] Stop `pacman -S python-*` from installing modules built for an older Python than 3.14.
- [x] Generate `en_US.UTF-8` next to `C.UTF-8` for programs that need a named locale.
- [x] Add fontconfig aliases, so programs asking for Arial, Helvetica, Times or Courier get a matching font.
- [x] Open `.exe`/`.com`, `.prg`/`.d64`, `.nes`, `.sfc`, `.gba` and `.md` files in the right emulator with `xdg-open`.
- [x] Set `TERM` so ssh from foot to machines without foot's terminfo works.
- [ ] Test in QEMU that AppImages, pip in a venv and `go install` programs run.
- [ ] Install and start the 50 most installed Arch packages in QEMU, and fix or list what fails.
