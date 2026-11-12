## Plans

- [ ] When drawing with the left mouse button on the desktop background, draw a performant version of the compiz flame effect.

### 1. Security

- [ ] Do not start dropbear by default: the `tinyhat` password is known. Add a menu entry that
      enables SSH only after a new password or a key is set.

### 2. Finish and verify the current build round

- [x] Get the full build through with the Python 3.14 build environment (i686, x32, x86_64, aarch64, rpi4, OVA, qemu tar.gz).
- [ ] Commit by theme once it passes (kernel, Python, dwl, DOS, Wine, build scripts).
- [ ] Test on real hardware: rounded teal window title, Pyxel on the Radeon (shader fallback),
      space/focus in NetSurfy, Lutris install without the cairo error.
- [ ] Test in QEMU: FreeDOS, "Download and run a game...", Turbo Pascal install, Pyxel on Ctrl+Alt+0,
      Orbiton boot mode, OVA guest additions and 3D.
- [ ] Retest kitty on the new kernel (llvmpipe only offers OpenGL 3.0 in the VM).

### 3. Phase out X11 and GTK3

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

### 4. Make the build trustworthy

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

### 5. Cross-build i686 from x86_64, and drop archlinux32

- [ ] Pilot: cross-build fuzzel, mpg123 and SDL3 for i686 from an x86_64 Arch environment against a sysroot
      of our own i686 packages, and compare with today's builds.
- [ ] Build everything that ends up in the image from source, then the sysroot libraries.
- [ ] Reduce archlinux32 to a one-time bootstrap of the first sysroot, then to nothing.
- [ ] Delete the heuristics in `mkpkgs.py` as they become dead: broken-in-the-seed, too-old rules,
      Python-bound packages, the `[buildroot] local` bootstrap.

### 6. TINYPACK

- [ ] Move the cross backend forward and pilot it on i686 (see 5).
- [ ] The converter folds `config/pkgbuild-overrides` and lines appended to PKGBUILDs into `tiny.lua`,
      so a recipe is one file. Arch's PKGBUILD then only feeds the autoupdate version check.
- [ ] Per-package checks for all recipes, then emit PKGBUILDs from `tiny.lua`, then convert the simple recipes.

### 7. Installers as packages

- [ ] A shared helper for the menu installers (pacman setup, download and verify), with the pins in a data file
      like `games.tsv` that autoupdate can check.
- [ ] A small Tiny Hat pacman repo, signed with tinyhat-keyring, carrying cnc-ddraw, JBR and CGoban as recipes,
      so the menu entries become `pacman -S`. Turbo Pascal keeps downloading at install time.
- [ ] Decide which of Wine, Battle.net, Lutris and Java are supported and which are best-effort.

### 8. Later

- [ ] x32 builds of DOSBox-X, ZSNES, ScummVM, mGBA and VICE for tinyhat64x32.
- [ ] Patch OpenTTD to SDL3, then consider including it.
- [ ] A Pyxel-based turtle module, since Python is built without tkinter (undecided).
- [ ] Man pages: compressing them makes the squashfs larger, so they stay uncompressed (undecided).
- [ ] innoextract and vulkan-icd-loader as on-demand additions to the Wine installer.
- [ ] Measure boot time on the retro PC.

## Earlier plans: implemented, waiting for the tests in 2

- [ ] `wget` as a fish alias for `curl -fLO` (wurl removed), `WINETRICKS_DOWNLOADER=curl` everywhere.
- [ ] Teal window title in the status bar.
- [ ] No percentages for volume and battery in the status bar.
- [ ] glfw-wayland (the glfw recipe provides it).
- [ ] Python REPL colours.
- [ ] Space for PgDn in NetSurfy, and delayed URL suggestions.
- [ ] OVA with guest additions and 3D acceleration.
