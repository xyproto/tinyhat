## Plans

### Yes

- [ ] Consider using gosh instead of bash.
- [ ] When right-clicking the desktop background, then the background image should cycle.
      But, when a doodle is active, the doodle should be cleared and the doodles reset instead.
- [ ] Move vendored files into build/ and download them as needed when building instead of including them
      into this git repository. They take up too much space, unfortunately. Or! Split up this repo into
      several git repositories.
- [ ] Reduce archlinux32 to a one-time bootstrap, then remove it and the `build_planner.py` heuristics it needs.
- [ ] Let Arch and archlinux32 packages install on top of Tiny Hat without replacing its packages.
      makep now writes versioned library provides (like `libcrypt.so=2-32`), but they only take effect when the
      library packages are rebuilt, so rebuild them. Then let cairo-wayland satisfy `cairo`, and use the same
      technique for other packages that Tiny Hat builds differently, so that prebuilt packages like Firefox install.
- [ ] Stop `pacman -S python-*` from installing modules built for an older Python than 3.14.
- [ ] python-pyxel with thin LTO on i586 without SSE fails in LLVM (Rust 1.99.1) with "Do not know how to split this
      operator's operand!"; the TINYBUILD sets CARGO_PROFILE_RELEASE_LTO=false. Reduce it to a small test case and
      report it to Rust/LLVM.
- [ ] Ask fish upstream to update chacha20 to 0.10.2 in Cargo.lock: 0.10.0 uses SSE4.1 in its SSE2 code, so fish
      crashed with SIGILL on CPUs with SSE2 but without SSE4.1. The fish TINYBUILD updates it for now.
- [ ] Make `import turtle` work in Python, with a custom turtle module that uses SDL3 instead of Tcl/Tk.
- [ ] Enable 3D acceleration by default for the OVA, and check that kitty, GLFW and similar programs still work.
- [ ] `pacman -S nethack` installs archlinux32's 5.0.0-1, which has Arch's packaging bug (binary wants
      /var/games/nethack/sysconf, the launcher has HACKDIR=/usr/lib/nethack; Arch fixed both in 5.0.0-2), and it is
      built for Lua 5.4.8 while Tiny Hat has 5.4.9, so it stops with "sandbox doesn't know this Lua version". Ask
      archlinux32 to rebuild 5.0.0-2, and decide whether Tiny Hat should ship its own nethack built for its Lua.

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
- [ ] Include Lutris from its GTK4 branch, with wlr-randr, cabextract, psmisc, Pillow and the Python request libraries.

### To test

- [ ] `sudo pacman -S kitty`, `sudo pacman -S firefox` and `sudo pacman -S lutris` work after booting tinyhat32 (once
      the versioned provides above are in place), and there is a menu entry for installing and starting Lutris.
      The same goes for Wine.
- [ ] On real hardware: the desktop doodles (flames and the faster shapes), and the GRUB "start fresh" submenu borders.
- [ ] Hat Toss on the FreeDOS disk with an Adlib or Sound Blaster card (it is silent without one).
