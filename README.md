# Tiny Hat

This is a Linux distro based on Arch Linux and archlinux32, with a few custom packages on top.

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

It also includes MemTest86+.

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

The licenses are in the `licenses` directory.

### General

* License: BSD-3
* Version: 0.2.0
