# Tiny Hat

<img alt="Logo" src="img/tinyhat.svg" width=150 align=right>

This is a Linux distro based on Arch Linux and [archlinux32](https://archlinux32.org/), with a few custom packages on top.

This `README.md` file, design choices and package selection is human made. AI tooling has been used for creating the Makefile, adding test scripts, debugging why some program fails to start etc.

Time consuming testing on real hardware has been done by a human (me).

### Goals

The goals for this distro are:

* Use less than 100M of memory after booting.
* Use around 500M of disk space (currently uses a bit more, but still usable on a 1GB USB stick).
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

It also includes MemTest86+.

### Reproducible builds

* Each release has a `SHA256SUMS` file with the SHA256 hash sum, as well as a detached GPG signature for the hash sum named `SHA256SUMS.asc`.
* One can veryfy the image hash sum file with `gpg --verify SHA256SUMS.asc SHA256SUMS` and then `sha256sum -c SHA256SUMS`.
* Each build of the image (`tinyhat32.img`). is reproducible and bit-identical.

### Dependencies

For `make` to be able to create `tinyhat32.img`, these Arch Linux packages are needed:

    base-devel bc bubblewrap cpio git libxml2-legacy python-numpy squashfs-tools vorbis-tools xfsprogs

For `make vdi`, this package is also needed:

    virtualbox

For `make iso`, this package is also needed:

    libisoburn

For `make run`, `make qemutest` and `make qemutest64x32`, these packages are also needed:

    imagemagick qemu-system-x86

For `make both` and `tinyhat64x32.img`, this package is also needed (for creating the Secure Boot signing key):

    openssl

For `make dist`, these packages are also needed (`virtualbox` is optional, for also creating `.vdi` files):

    gnupg gtk4 libisoburn mingw-w64-gcc openssl

For `make autoupdate`, these packages are also needed:

    nvchecker pacman-contrib python-jq python-lxml

### Installation

#### From Linux, UNIX or FreeBSD

* Download the `tinyhat32.img` file (or clone this repo, install all required dependencies and run `make`).
* Run `sudo fdisk -l` to list all drives, try to identify the USB drive you want to flash the image to.
* Remember the drive name, for example `/dev/sdq`. Be 100% sure to use the USB drive and not an internal harddrive!
* Run this command, but replace `sdq` with your drive. Please wait and think for 3 seconds and double check the `of` value before pressing return:
   * `sudo dd if=tinyhat32.img of=/dev/sdq bs=4M conv=fsync status=progress`

#### From Windows

* Download and run the `.exe` from the [release page](https://github.com/xyproto/tinyhat/releases), which includes the image and a tiny GUI program for writing the image to a removable drive. (Experimental feature, needs testing).

### Booting

* Just plop the USB drive into the x86 computer you wish to boot and turn on the computer.
* If needed, press F10, Del or F2 repeatedly at boot to get into the BIOS, where it is possible to select that the system should boot from the USB drive before the harddrive.

### Licenses

The licenses are generated during the build (`make licenses` writes them to `build/licenses`). In Tiny Hat they are in `/usr/share/licenses/tinyhat`, and each release includes them as `tinyhat-licenses-<version>.tar.gz`.

### General

* Version: 1.0.4
* License: BSD-3
