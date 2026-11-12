## Plans

- [ ] alt-left and alt-right should work in NetSurfy to go back/forward.
- [ ] Just "ls" should not list with details, by default.

### Various

- [ ] When booting straight into Orbiton, ctrl-q should play the shutdown animation and reboot, instead of just shutting down.
- [ ] Let all QEMU tests etc. specify the keyboard layout before sending keypresses, for instance ctrl-alt-k "English (US)".
- [x] altgr+4 to draw "$" no longer works with the Norwegian keyboard layout!
- [ ] Let the help text background frame be centered vertically (minus the status bar height)
- [ ] When drawing with the left mouse button on the desktop background, draw a performant version of the compiz flame effect.
- [ ] Also mention win-f and alt-shift-f to toggle maximized on the help background.
- [ ] When starting monkeyjumpgo, also show a window with the keybindings, the first time it is started + console output.
- [ ] Add a link to the interactive way to go to the alt-space menu.
- [ ] Add a shortcut to starting monkeyjumpgo with a 19x19 board to the alt-space menu.
- [ ] By default, the mouse buttons can go forward/backwards, like now, but it would be fun if the user
      could configure them to start a desktop shortcut, run a command, adjust the volume,
      toggle floating/tiled mode etc. This could be configured in the dwl toml config file.
- [ ] It would be nice if the volume icon tooltip also showed if pipewire was loaded or not.
- [ ] There is a bit too much horizontal space to the right side of the volume icon. It would be nice
      if the margin was based on non-transparent pixels in the image, and not just on the image data width.
- [ ] The boot animation gallery / editor launcher / configurator needs many small improvements.
      It currently does not work. Consider splitting everything that has to do with boot/shutdown animations
      out to a separate repository. It would be fun if it worked on Arch Linux too.
- [ ] Commit by theme once it passes (kernel, Python, dwl, DOS, Wine, build scripts).
- [ ] Test on real hardware: rounded teal window title, Pyxel on the Radeon (shader fallback),
      space/focus in NetSurfy, Lutris install without the cairo error.
- [ ] Test in QEMU: FreeDOS, "Download and run a game...", Turbo Pascal install, Pyxel on Ctrl+Alt+0,
      Orbiton boot mode, OVA guest additions and 3D.
- [ ] Retest kitty on the new kernel (llvmpipe only offers OpenGL 3.0 in the VM).
- [ ] Apply the prepared installer changes: Wine uses Wayland first (`wayland,x11`) and does not install XWayland;
      Java uses JetBrains Runtime with the Wayland toolkit on 64-bit, so CGoban 3 needs no X11 there.
- [ ] Test CGoban 3 on JBR under dwl: fullscreen, closing, the KGS login window.
- [ ] Include Lutris, built from upstream's GTK4 branch with the Tiny Hat patch (wlr-randr display backend,
      WebKit optional). Disable its 483 MB runtime download by default (`LUTRIS_RUNTIME=0`).
- [ ] Add what Lutris needs: wlr-randr, cabextract, psmisc, Pillow, and i686 recipes for requests, urllib3,
      idna, charset-normalizer and certifi.
- [ ] Then remove cairo's X11 backend and libxext/libxrender from the images again.
- [ ] Go through the image locks and recipes for anything else that pulls in X11 or GTK3.
- [ ] Test Wine with StarCraft 1.16.1 (cnc-ddraw) and Battle.net/Remastered on tinyhat64.
- [ ] Verify in this build round: `sonamecheck.py` (newer libraries than Arch must keep Arch's version too, like
      libdisplay-info0.3) and `startcheck` in the QEMU test (Python, Lutris, Pyxel, gdb and every menu entry start).
- [ ] Guru meditation crash screen (`tinyhat-guru`) for dwl crash loops and initramfs rescue, guru-style kernel panic
      text; test each path, including the QR code on a real text console.
- [ ] dwl: rounded workspace indicator, and no crash when a screen has no modes (VirtualBox with 3D on).
- [ ] `DRAW_USE_LLVM=0` only for hardware renderers: retest kitty and OpenGL 4.6 on llvmpipe.
- [ ] Battle.net: test whether Wine honours Image File Execution Options, to add `--in-process-gpu` without menu entries.
- [ ] Software Vulkan (lavapipe) finds no device in the VM; find out why.
- [ ] Build sandboxes install exactly the versions the plan names, not the newest file in `build/repo`.
- [ ] A pre-flight that verifies the source checksums of all recipes before a build starts.
- [ ] Resolve `provides` in one place instead of separately in several scripts.
- [ ] QEMU checks for things found by hand: window title width, space in NetSurfy, the menu installers.
- [ ] Pilot: cross-build fuzzel, mpg123 and SDL3 for i686 from an x86_64 Arch environment against a sysroot
      of our own i686 packages, and compare with today's builds.
- [ ] Build everything that ends up in the image from source, then the sysroot libraries.
- [ ] Reduce archlinux32 to a one-time bootstrap of the first sysroot, then to nothing.
- [ ] Delete the heuristics in `mkpkgs.py` as they become dead: broken-in-the-seed, too-old rules,
      Python-bound packages, the `[buildroot] local` bootstrap.
- [ ] Move the cross backend forward and pilot it on i686 (see 5).
- [ ] The converter folds `config/pkgbuild-overrides` and lines appended to PKGBUILDs into `tiny.lua`,
      so a recipe is one file. Arch's PKGBUILD then only feeds the autoupdate version check.
- [ ] Per-package checks for all recipes, then emit PKGBUILDs from `tiny.lua`, then convert the simple recipes.
- [ ] A shared helper for the menu installers (pacman setup, download and verify), with the pins in a data file
      like `games.tsv` that autoupdate can check.
- [ ] A small Tiny Hat pacman repo, signed with tinyhat-keyring, carrying cnc-ddraw, JBR and CGoban as recipes,
      so the menu entries become `pacman -S`. Turbo Pascal keeps downloading at install time.
- [ ] Decide which of Wine, Battle.net, Lutris and Java are supported and which are best-effort.
- [ ] x32 builds of DOSBox-X, ZSNES, ScummVM, mGBA and VICE for tinyhat64x32.
- [ ] Patch OpenTTD to SDL3, then consider including it.
- [ ] A Pyxel-based turtle module, since Python is built without tkinter (undecided).
- [ ] innoextract and vulkan-icd-loader as on-demand additions to the Wine installer.
- [ ] Measure boot time on the retro PC.
- [ ] `wget` as a fish alias for `curl -fLO` (wurl removed), `WINETRICKS_DOWNLOADER=curl` everywhere.
- [ ] Teal window title in the status bar.
- [ ] No percentages for volume and battery in the status bar.
- [ ] glfw-wayland (the glfw recipe provides it).
- [ ] Python REPL colours.
- [ ] Space for PgDn in NetSurfy, and delayed URL suggestions.
- [ ] OVA with guest additions and 3D acceleration.
