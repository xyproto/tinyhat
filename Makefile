VERSION := 1.0.4
KVER := 7.3
export SOURCE_DATE_EPOCH := $(shell git log -1 --format=%ct 2>/dev/null || echo 315532800)
DIST_HOME ?= https://github.com/xyproto/tinyhat
B := build
REPO := $(B)/repo
SOUNDFONT_GU := vendor/soundfont/GeneralUser-GS-2.0.3.sf3
OVERLAY := $(shell find config/overlay)
SOURCES := $(addprefix vendor/src/,$(shell python3 scripts/distro.py sources | awk '{ print $$1 }'))
QEMU_MEM ?= 1024
# Disk space and memory while building: how many packages build at once (one per 8 GB of memory, at least 1),
# how many compile jobs each of them runs (the CPU cores shared between them), the ccache size, and whether
# downloaded package sources and the kernel build tree are kept after building (KEEP_SOURCES=1).
# With 16 GB of memory and 12 cores, that is 2 packages at once with 6 jobs each.
ifeq ($(origin PKG_JOBS),undefined)
PKG_JOBS := $(shell awk '/^MemTotal:/ { n = int($$2 / 1048576 / 8 + 0.25); print (n < 1 ? 1 : n) }' /proc/meminfo)
endif
ifeq ($(origin PKG_MAKE_JOBS),undefined)
PKG_MAKE_JOBS := $(shell n=$$(( $$(nproc) / $(PKG_JOBS) )); echo $$(( n < 1 ? 1 : n )))
endif
export PKG_JOBS PKG_MAKE_JOBS
export CCACHE_MAXSIZE ?= 2G
export KEEP_SOURCES ?= 0
SB_KEYS ?= $(HOME)/.local/share/tinyhat/secureboot
BOOT_PKGS := $(B)/pkg/i686/grub $(B)/pkg/i686/linux-firmware $(B)/pkg/i686/intel-ucode $(B)/pkg/i686/memtest86+ $(B)/pkg/i686/kbd
IMG_DEPS := $(B)/theme.toml config/grub.cfg config/grub-early.cfg scripts/mkimg.sh scripts/mkgrubtheme.py scripts/mbr.py scripts/xfsnormalize.py $(BOOT_PKGS)

ifeq (,$(findstring -j,$(MAKEFLAGS)))
MAKEFLAGS += -j$(shell nproc)
endif

.DELETE_ON_ERROR:
.PHONY: all iso both rpi4 aarch64packages autoupdate lock lock-aarch64 lock-check fetch sources licenses packages x32packages srcinfo import run qemutest qemutest64x32 cleantest cleanchroot status clean distclean patchcheck sourcecheck fuzzelcheck test dist distbuild sign vdi

all: tinyhat32.img

iso: tinyhat32.iso

both: tinyhat32.img tinyhat64x32.img

rpi4: tinyhat-rpi4.img

# Goals that only read or update the configuration do not need build/pkgs.mk (which needs the buildroot)
ifneq (,$(filter-out cleantest cleanchroot clean distclean status lock lock-check lock-aarch64 import srcinfo autoupdate sign,$(or $(MAKECMDGOALS),all)))
include $(B)/pkgs.mk
endif

$(B)/pkgs.mk: $(B)/seed-broken.txt scripts/mkpkgs.py scripts/pkg.py scripts/distro.py scripts/srcinfo.py distro.toml distro.lock $(wildcard packages/*/*)
	@mkdir -p $(B)
	python3 scripts/srcinfo.py
	python3 scripts/mkpkgs.py mk >$@

tinyhat32.img: $(B)/vmlinuz $(B)/initramfs.img $(B)/root.sfs $(IMG_DEPS)
	scripts/mkimg.sh $@ $(B)/vmlinuz $(B)/initramfs.img $(B)/root.sfs
	@ls -l $@

tinyhat-rpi4.img: $(B)/initramfs-aarch64.img $(B)/root-aarch64.sfs $(B)/.buildroot scripts/mkimg-rpi.sh scripts/mkdisk.sh scripts/xfsnormalize.py config/rpi/config.txt config/rpi/cmdline.txt
	scripts/mkimg-rpi.sh $@ $(B)/initramfs-aarch64.img $(B)/root-aarch64.sfs
	@ls -l $@

vdi: tinyhat32.vdi

tinyhat32.vdi: tinyhat32.img
	rm -f $@.tmp
	VBoxManage convertfromraw $< $@.tmp --format VDI
	mv $@.tmp $@
	@ls -l $@

tinyhat64x32.img: $(B)/vmlinuz-x32 $(B)/initramfs.img $(B)/root64.sfs $(IMG_DEPS) $(B)/pkg/i686/grub-efi64 $(B)/pkg/i686/sbsigntools vendor/src/shim-x64-16.1-10.x86_64.rpm $(SB_KEYS)/tinyhat.cer
	scripts/mkimg.sh $@ $(B)/vmlinuz-x32 $(B)/initramfs.img $(B)/root64.sfs efi64 $(SB_KEYS)
	@ls -l $@

$(SB_KEYS)/tinyhat.cer:
	scripts/mksbkey.sh $(SB_KEYS)

lock:
	@mkdir -p $(B)
	python3 scripts/mkpkgs.py seed >$(B)/seed.txt
	python3 scripts/mkpkgs.py libs
	python3 scripts/pkg.py lock buildroot $(B)/seed.txt

# Lock the prebuilt Arch Linux ARM packages for the aarch64 (Raspberry Pi 4) image
lock-aarch64:
	python3 scripts/mkpkgs.py lock-arch aarch64

lock-check:
	@mkdir -p $(B)
	@cp distro.lock $(B)/distro.lock.before
	@$(MAKE) -s lock || { cp $(B)/distro.lock.before distro.lock; exit 1; }
	@if cmp -s distro.lock $(B)/distro.lock.before; then echo "distro.lock is up to date"; \
	else cp $(B)/distro.lock.before distro.lock; echo "distro.lock is stale, run make lock" >&2; exit 1; fi

$(B)/theme.toml: distro.toml scripts/distro.py
	@mkdir -p $(B)
	python3 scripts/distro.py view theme $@

$(B)/image.toml: distro.toml scripts/distro.py
	@mkdir -p $(B)
	python3 scripts/distro.py view image $@

$(B)/buildroot.lock: distro.lock scripts/distro.py
	@mkdir -p $(B)
	python3 scripts/distro.py lockfile buildroot $@

$(B)/pkg/i686/tinyhat-splash: $(B)/theme.toml

import:
	python3 scripts/importpkg.py all

# Update recipes and the lock to newer package versions, holding back updates that hit [image] deny
autoupdate:
	python3 scripts/autoupdate.py

srcinfo:
	python3 scripts/srcinfo.py packages/*/

patchcheck: $(B)/.buildroot
	scripts/patchcheck.sh

sourcecheck:
	scripts/sourcecheck.sh

fuzzelcheck:
	scripts/fuzzelcheck.sh

test: patchcheck sourcecheck fuzzelcheck
	@for f in config/overlay/usr/bin/* scripts/*.sh; do sh -n "$$f" || exit 1; done
	@echo "shell scripts: ok"
	@scripts/chroot.sh $(B)/buildroot --bind $(CURDIR)/config /cfg grub-script-check /cfg/grub.cfg
	@echo "grub.cfg: ok"

dist: distbuild
	@$(MAKE) --no-print-directory sign

distbuild: tinyhat32.img tinyhat64x32.img tinyhat32.iso tinyhat64x32.iso
	DIST_HOME=$(DIST_HOME) DIST_URL=$(DIST_HOME)/releases/download scripts/mkdist.sh $(VERSION) $^
	@ls -l $(B)/dist

sign:
	@test -f $(B)/dist/SHA256SUMS || { echo "make sign: no $(B)/dist/SHA256SUMS, run make distbuild first" >&2; exit 1; }
	gpg --batch --yes --armor --detach-sign $${GPG_KEY:+-u "$$GPG_KEY"} -o $(B)/dist/SHA256SUMS.asc $(B)/dist/SHA256SUMS
	@ls -l $(B)/dist/SHA256SUMS.asc

tinyhat32.iso: tinyhat32.img scripts/mkiso.sh config/grub.cfg config/grub-early.cfg
	scripts/mkiso.sh $< $@

tinyhat64x32.iso: tinyhat64x32.img scripts/mkiso.sh config/grub.cfg config/grub-early.cfg
	scripts/mkiso.sh $< $@

fetch: $(B)/.fetched-buildroot sources

sources: $(SOURCES)

vendor/src/%:
	python3 scripts/pkg.py source vendor/src $*

$(B)/.fetched-buildroot: $(B)/buildroot.lock
	@mkdir -p $(B)
	python3 scripts/pkg.py fetch $< vendor/pkg
	@touch $@

$(B)/.buildroot: $(B)/.fetched-buildroot scripts/postinstall.sh scripts/rust.sh vendor/src/rust-1.99.1-i686-unknown-linux-gnu.tar.xz
	chmod -R u+w $(B)/buildroot 2>/dev/null || true
	rm -rf $(B)/buildroot
	python3 scripts/pkg.py extract $(B)/buildroot.lock vendor/pkg $(B)/buildroot
	ldconfig -r $(B)/buildroot
	scripts/postinstall.sh $(B)/buildroot $(B)/buildroot.lock vendor/pkg
	scripts/rust.sh $(B)/buildroot vendor/src/rust-1.99.1-i686-unknown-linux-gnu.tar.xz
	@touch $@

$(B)/buildroot-aarch64.lock: distro.lock scripts/distro.py
	@mkdir -p $(B)
	python3 scripts/distro.py lockfile buildroot-aarch64 $@

$(B)/.fetched-buildroot-aarch64: $(B)/buildroot-aarch64.lock
	python3 scripts/pkg.py fetch $< vendor/pkg-aarch64 aarch64
	@touch $@

$(B)/.buildroot-aarch64: $(B)/.fetched-buildroot-aarch64 scripts/postinstall.sh
	chmod -R u+w $(B)/buildroot-aarch64 2>/dev/null || true
	rm -rf $(B)/buildroot-aarch64
	python3 scripts/pkg.py extract $(B)/buildroot-aarch64.lock vendor/pkg-aarch64 $(B)/buildroot-aarch64
	scripts/postinstall.sh $(B)/buildroot-aarch64 $(B)/buildroot-aarch64.lock vendor/pkg-aarch64
	@touch $@

$(B)/seed-broken.txt: $(B)/.buildroot scripts/seedcheck.py
	python3 scripts/seedcheck.py $(B)/buildroot $(B)/buildroot.lock vendor/pkg $@

packages: $(I686_STAMPS)

x32packages: $(ROOTFS_X32_STAMPS)

aarch64packages: $(AARCH64_STAMPS)

$(B)/rootfs.lock: $(ROOTFS_I686_STAMPS) $(B)/image.toml
	python3 scripts/pkg.py local image $@ $(REPO)

$(B)/rootfs-x32.lock: $(ROOTFS_X32_STAMPS) $(B)/image.toml
	python3 scripts/pkg.py local image $@ $(REPO) x32

$(B)/rootfs-aarch64.lock: $(AARCH64_STAMPS) distro.lock
	python3 scripts/pkg.py archrepo aarch64 $(B)/repo-aarch64 $@

$(B)/vmlinuz: config/kernel-i686.fragment vendor/src/linux-$(KVER).tar.xz scripts/mkkernel.sh scripts/kcheck.py vendor/src/xpadneo-0.10.4.tar.gz
	@mkdir -p $(B)
	scripts/mkkernel.sh $(KVER) i386 $(B)/vmlinuz $(CURDIR)/config/kernel-i686.fragment

$(B)/vmlinuz-x32: config/kernel-i686.fragment config/kernel-x32.fragment vendor/src/linux-$(KVER).tar.xz scripts/mkkernel.sh scripts/kcheck.py vendor/src/xpadneo-0.10.4.tar.gz
	@mkdir -p $(B)
	scripts/mkkernel.sh $(KVER) x86_64 $(B)/vmlinuz-x32 $(CURDIR)/config/kernel-i686.fragment $(CURDIR)/config/kernel-x32.fragment

$(B)/initramfs.img: $(B)/pkg/i686/busybox $(B)/pkg/i686/tinyhat-splash $(B)/rootfs.lock config/initramfs/init scripts/mkinitramfs.sh
	scripts/mkinitramfs.sh $@

$(B)/initramfs-aarch64.img: $(B)/rootfs-aarch64.lock config/initramfs/init scripts/mkinitramfs.sh
	scripts/mkinitramfs.sh $@ aarch64

$(SOUNDFONT_GU): | vendor/src/GeneralUser-GS-2.0.3.sf2
	@mkdir -p $(dir $@)
	python3 scripts/sf2tosf3.py vendor/src/GeneralUser-GS-2.0.3.sf2 $@

licenses: $(B)/.licenses-i686

LICENSE_DEPS := $(BOOT_PKGS) $(B)/pkg/i686/busybox $(B)/pkg/i686/tinyhat-splash $(B)/pkg/i686/licenses $(SOURCES) scripts/mklicenses.py

$(B)/.licenses-i686: $(B)/rootfs.lock $(LICENSE_DEPS)
	python3 scripts/mklicenses.py $(B)/rootfs.lock $(B)/licenses
	@touch $@

$(B)/.licenses-x32: $(B)/rootfs-x32.lock $(LICENSE_DEPS) $(B)/pkg/i686/grub-efi64
	python3 scripts/mklicenses.py $(B)/rootfs-x32.lock $(B)/licenses-x32
	@touch $@

$(B)/.licenses-aarch64: $(B)/rootfs-aarch64.lock $(B)/pkg/i686/licenses $(SOURCES) scripts/mklicenses.py
	python3 scripts/mklicenses.py $(B)/rootfs-aarch64.lock $(B)/licenses-aarch64 aarch64
	@touch $@

ROOTFS_BASE_DEPS := $(SOUNDFONT_GU) vendor/src/pixelitos-icon-theme-20260721.tar.gz scripts/mkpixelitos.py vendor/src/BASS-Floppy-1.3.zip vendor/src/lure-1.1.zip vendor/src/soltys-en-v1.0.zip vendor/src/beej-bgc.zip vendor/src/beej-bgclr.zip vendor/src/open-watcom-2026-10-01.tar.xz $(B)/image.toml $(B)/theme.toml scripts/mkrootfs.sh scripts/mkwallpaper.py scripts/mkhatwallpaper.py scripts/mkhundetann.py img/tinyhat.svg scripts/inroot.sh scripts/mklocaldb.py
ROOTFS_FINAL_DEPS := $(B)/theme.toml $(OVERLAY) config/rofi/config.rasi packages/tinyhat-splash/hat.c packages/tinyhat-splash/hat.h scripts/elfcheck.py scripts/symcheck.sh

$(B)/rootfs.base.stamp: $(ROOTFS_BASE_DEPS) $(B)/rootfs.lock $(B)/vmlinuz
	TINYHAT_PKGS="$(TINYHAT_PKGS)" scripts/userns.sh scripts/mkrootfs.sh base $(B)/rootfs $(B)/modules $(B)/root.sfs $(B)/rootfs.lock $(B)/licenses
	touch $@

$(B)/rootfs-x32.base.stamp: $(ROOTFS_BASE_DEPS) $(B)/rootfs-x32.lock $(B)/vmlinuz-x32
	TINYHAT_PKGS="$(TINYHAT_PKGS)" scripts/userns.sh scripts/mkrootfs.sh base $(B)/rootfs64 $(B)/modules-x86_64 $(B)/root64.sfs $(B)/rootfs-x32.lock $(B)/licenses-x32
	touch $@

$(B)/rootfs-aarch64.base.stamp: $(ROOTFS_BASE_DEPS) $(B)/rootfs-aarch64.lock
	@mkdir -p $(B)/modules-aarch64
	TINYHAT_PKGS="$(TINYHAT_PKGS)" scripts/userns.sh scripts/mkrootfs.sh base $(B)/rootfs-aarch64 $(B)/modules-aarch64 $(B)/root-aarch64.sfs $(B)/rootfs-aarch64.lock $(B)/licenses-aarch64
	touch $@

$(B)/root.sfs: $(B)/rootfs.base.stamp $(ROOTFS_FINAL_DEPS) $(B)/.licenses-i686
	TINYHAT_PKGS="$(TINYHAT_PKGS)" scripts/userns.sh scripts/mkrootfs.sh finalize $(B)/rootfs $(B)/modules $@ $(B)/rootfs.lock $(B)/licenses
	@ls -l $@

$(B)/root64.sfs: $(B)/rootfs-x32.base.stamp $(ROOTFS_FINAL_DEPS) $(B)/.licenses-x32
	TINYHAT_PKGS="$(TINYHAT_PKGS)" scripts/userns.sh scripts/mkrootfs.sh finalize $(B)/rootfs64 $(B)/modules-x86_64 $@ $(B)/rootfs-x32.lock $(B)/licenses-x32
	@ls -l $@

$(B)/root-aarch64.sfs: $(B)/rootfs-aarch64.base.stamp $(ROOTFS_FINAL_DEPS) $(B)/.licenses-aarch64
	TINYHAT_PKGS="$(TINYHAT_PKGS)" scripts/userns.sh scripts/mkrootfs.sh finalize $(B)/rootfs-aarch64 $(B)/modules-aarch64 $@ $(B)/rootfs-aarch64.lock $(B)/licenses-aarch64
	@ls -l $@

$(B)/test-usb.img: tinyhat32.img
	cp --sparse=always tinyhat32.img $@
	truncate -s 4G $@

run: $(B)/test-usb.img
	qemu-system-i386 -enable-kvm -cpu host -smp 2 -m $(QEMU_MEM) \
		-drive if=none,id=stick,file=$(B)/test-usb.img,format=raw \
		-device qemu-xhci -device usb-storage,drive=stick,bootindex=0 \
		-device usb-tablet -vga std \
		-audiodev pa,id=snd -device intel-hda -device hda-duplex,audiodev=snd \
		-nic user,model=e1000

qemutest: tinyhat32.img
	python3 scripts/qemutest.py tinyhat32.img

qemutest64x32: tinyhat64x32.img
	python3 scripts/qemutest.py tinyhat64x32.img

cleantest:
	rm -rf $(B)/qmt $(B)/test-usb.img

cleanchroot:
	scripts/userns.sh rm -rf $(B)/chroot

status:
	@python3 scripts/buildstatus.py tinyhat32.img
	@python3 scripts/buildstatus.py tinyhat64x32.img

clean:
	scripts/userns.sh rm -rf $(B)
	rm -f tinyhat32.img tinyhat32.img.tmp tinyhat32.vdi tinyhat32.vdi.tmp tinyhat64x32.img tinyhat64x32.img.tmp tinyhat-rpi4.img tinyhat-rpi4.img.tmp tinyhat32.iso tinyhat64x32.iso

distclean: clean
	rm -rf vendor/pkg vendor/pkg-aarch64
