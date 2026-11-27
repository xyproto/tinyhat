<img alt="Logo" src="img/tinyhat.svg" width=32>

# Tiny Hat

This is a Linux distro based on Arch Linux, [archlinux32](https://archlinux32.org/) and Arch Linux ARM, with a few custom packages on top.

<img width="1280" height="800" alt="Tiny Hat 1 0 7" src="https://github.com/user-attachments/assets/dbc46fa9-a330-46a1-9eb2-efa562b9cfd3" />

Please [recommend Tiny Hat on DistroWatch](https://distrowatch.com/dwres.php?resource=links#new) if you find it fun. :)

### Goals

The goals for this distro are:

* Use less than 100M of memory after booting.
* Use around 600M of disk space (currently uses a bit more, but still usable on a 1GB USB stick).
* Use modern libraries, frameworks and protocols (SDL3, Wayland).
* To be possible to use on a 32-bit or 64-bit x86 machine with little ram and disk space, for:
  * Retro gaming (ScummVM, DosBox-X and ZSNES).
  * Playing whichever DOS games are on the first harddrive, after booting from an USB stick.
  * Creating graphics / pixel art (LibreSprite).
  * Creating music / tracking (Fast Tracker II / ft2-clone).
  * Programming in C with SDL3 and/or Raylib and/or fluidsynth, with tab completion in Orbiton.
  * Debugging with gdb and Orbiton.
  * Programming in Assembly with nasm.
  * Programming for SNES with cc65.
  * Game programming with Fennel and LÖVE.
  * Using a simple browser (Netsurf) + packet sniffer (jomon) + ssh client / server / scp (dropbear).
* Be straightforward to use for old DOS and Arch Linux users alike.
* Be not impossible to use for brand new users.
* Have reproducible builds.

Its boot menu also has MemTest86+ and a FreeDOS 1.4 live floppy with a couple of games.

### Reproducible builds

* Each release has a `SHA256SUMS` file with the SHA256 hash sum, as well as a detached GPG signature for the hash sum named `SHA256SUMS.asc`.
* One can veryfy the image hash sum file with `gpg --verify SHA256SUMS.asc SHA256SUMS` and then `sha256sum -c SHA256SUMS`.
* Each build of the image (`build/tinyhat32.img`). is reproducible and bit-identical.

### Dependencies

Building needs an Arch Linux host with `base` and `base-devel`. The other build tools come from the build environment
that the build sets up from prebuilt Arch Linux packages. `make` checks for the packages below and names any that are
missing.

For `make 32`, `make x32`, `make 64` and the other images:

    bubblewrap cpio fish git ninja python

`make dist` also needs a GnuPG secret key, for signing `SHA256SUMS`.

For `make run`, `make qemutest` and `make boottest`, these packages are also needed:

    edk2-aarch64 imagemagick qemu-system-aarch64 qemu-system-x86

For `make autoupdate`, these packages are needed along with `fish`, `git` and `python`:

    nvchecker pacman-contrib python-jq python-lxml

### Installation

#### From Linux, UNIX or FreeBSD

* Download the `tinyhat32.img` file (or clone this repo, install the dependencies and run `make 32`).
* Run `sudo fdisk -l` to list all drives, try to identify the USB drive you want to flash the image to.
* Remember the drive name, for example `/dev/sdq`. Be 100% sure to use the USB drive and not an internal harddrive!
* Run this command, but replace `sdq` with your drive. Please wait and think for 3 seconds and double check the `of` value before pressing return:
   * `sudo dd if=tinyhat32.img of=/dev/sdq bs=4M conv=fsync status=progress`

#### From Windows

* Download the `.img.gz` file from the [release page](https://github.com/xyproto/tinyhat/releases).
* Write it to a USB drive with [Rufus](https://rufus.ie/) or [Raspberry Pi Imager](https://www.raspberrypi.com/software/) (choose "Use custom"). Both are signed programs that most Windows systems, including company administered ones, allow.
* Or skip the USB drive and run the `.vdi` file in [VirtualBox](https://www.virtualbox.org/).

### Booting

* Just plop the USB drive into the x86 computer you wish to boot and turn on the computer.
* If needed, press F10, Del or F2 repeatedly at boot to get into the BIOS, where it is possible to select that the system should boot from the USB drive before the harddrive.

### Tooling

This `README.md` file, design choices and package selection is human made. AI tooling has been used for creating the `Makefile`, adding test scripts, debugging why some program fails to start etc. Time consuming testing on real hardware has been done by a human (me).

### Licenses

The licenses are generated during the build (`make licenses` writes them to `build/licenses`). In Tiny Hat they are in `/usr/share/licenses/tinyhat`, and each release includes them as `tinyhat-licenses-<version>.tar.gz`.

### General

* Version: 1.0.9
* License: BSD-3
