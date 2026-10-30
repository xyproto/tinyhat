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
VDI_SIZE ?= 20G
# Disk space and memory while building: how many packages build at once (one per 8 GB of memory, at least 1),
# how many compile jobs each of them may run (all cores, held back by the load limit PKG_LOAD so that packages
# building at the same time share the cores), the ccache size, and whether downloaded package sources and the
# kernel build tree are kept after building (KEEP_SOURCES=1).
ifeq ($(origin PKG_JOBS),undefined)
PKG_JOBS := $(shell awk '/^MemTotal:/ { n = int($$2 / 1048576 / 8 + 0.25); print (n < 1 ? 1 : n) }' /proc/meminfo)
endif
ifeq ($(origin PKG_MAKE_JOBS),undefined)
PKG_MAKE_JOBS := $(shell nproc)
endif
PKG_LOAD ?= $(shell nproc)
PKG_CARGO_JOBS ?= $(shell n=$$(( $$(nproc) / $(PKG_JOBS) )); echo $$(( n < 1 ? 1 : n )))
export PKG_JOBS PKG_MAKE_JOBS PKG_LOAD PKG_CARGO_JOBS
export CCACHE_MAXSIZE ?= 20G
export KEEP_SOURCES ?= 0
SB_KEYS ?= $(HOME)/.local/share/tinyhat/secureboot
BOOT_PKGS := $(B)/pkg/i686/grub $(B)/pkg/i686/linux-firmware $(B)/pkg/i686/intel-ucode $(B)/pkg/i686/memtest86+ $(B)/pkg/i686/kbd
IMG_DEPS := $(B)/theme.toml config/grub.cfg config/grub-early.cfg scripts/mkimg.sh scripts/mkgrubtheme.py scripts/mbr.py scripts/xfsnormalize.py $(BOOT_PKGS)

ifeq (,$(findstring -j,$(MAKEFLAGS)))
MAKEFLAGS += -j$(shell nproc)
endif

IMG_I686 := tinyhat32.img
IMG_X32 := tinyhat64x32.img
IMG_AARCH64 := tinyhat-aarch64.img
IMG_RPI4 := tinyhat-rpi4.img
IMGS := $(IMG_I686) $(IMG_X32) $(IMG_AARCH64) $(IMG_RPI4)
ISOS := $(IMG_I686:.img=.iso) $(IMG_X32:.img=.iso) $(IMG_AARCH64:.img=.iso)
VDIS := $(IMG_I686:.img=.vdi) $(IMG_X32:.img=.vdi) $(IMG_AARCH64:.img=.vdi)

.DELETE_ON_ERROR:
.DEFAULT_GOAL := help
.PHONY: help all img iso vdi i686 x32 32 64 aarch64 rpi4 i686_iso x32_iso aarch64_iso aarch64packages autoupdate lock lock-aarch64 lock-check fetch sources licenses packages x32packages srcinfo import run boottest qemutest boottest-i686 boottest-x32 boottest-aarch64 boottest-rpi4 cleantest cleanchroot status clean distclean patchcheck sourcecheck fuzzelcheck test dist distbuild sign

C_HEAD := \033[1;31m
C_HEAD2 := \033[1;34m
C_GRP := \033[1;36m
C_TGT := \033[1;33m
C_OFF := \033[0m
comma := ,
define target
	@printf '  $(C_TGT)%-14s$(C_OFF) %s\n' $(1) $(2)
endef

help:
	@printf '\n$(C_HEAD)Tiny Hat $(VERSION)$(C_OFF)\n'
	@printf '$(C_HEAD2)A tiny Linux distro for retro gamedev on limited devices$(C_OFF)\n\n'
	@printf '$(C_GRP)Images$(C_OFF)\n'
	$(call target,32,"32-bit x86 image ($(IMG_I686))$(comma) (make i686 also works)")
	$(call target,64,"64-bit x86 with 32-bit pointers image ($(IMG_X32))$(comma) (make x32 also works)")
	$(call target,arm64,"64-bit ARM UEFI image ($(IMG_AARCH64)) (make aarch64 also works)")
	$(call target,rpi4,"64-bit ARM Raspberry Pi 4 image ($(IMG_RPI4))")
	@printf '\n$(C_GRP)CD/DVD/USB ISO images$(C_OFF)\n'
	$(call target,i686_iso,"32-bit x86 ISO")
	$(call target,x32_iso,"x32 ISO")
	$(call target,aarch64_iso,"64-bit ARM UEFI ISO")
	$(call target,iso,"all three ISO images")
	@printf '\n$(C_GRP)Groups and releases$(C_OFF)\n'
	$(call target,vdi,"VirtualBox disk images (i686$(comma) x32$(comma) aarch64)")
	$(call target,all,"every .img$(comma) .iso and .vdi")
	$(call target,dist,"release files in $(B)/dist$(comma) signed")
	@printf '\n$(C_GRP)Packages and maintenance$(C_OFF)\n'
	$(call target,packages,"build or fetch every package the images need")
	$(call target,lock,"update distro.lock")
	$(call target,lock-check,"check that distro.lock is up to date")
	$(call target,autoupdate,"update recipes to newer versions")
	$(call target,test,"run the checks")
	$(call target,boottest,"boot every image headless in QEMU (also boottest-i686 and so on)")
	$(call target,qemutest,"boot the i686$(comma) x32 and aarch64 images in QEMU$(comma) one at a time")
	$(call target,run,"boot the i686 image in QEMU")
	$(call target,status,"show what still needs building")
	$(call target,clean,"remove build output")
	@printf '\n'

i686: $(IMG_I686)

x32: $(IMG_X32)

32: i686

64: x32

arm64: aarch64

aarch64: $(IMG_AARCH64)

rpi4: $(IMG_RPI4)

i686_iso: $(IMG_I686:.img=.iso)

x32_iso: $(IMG_X32:.img=.iso)

aarch64_iso: $(IMG_AARCH64:.img=.iso)

img: $(IMGS)

iso: $(ISOS)

vdi: $(VDIS)

all: $(IMGS) $(ISOS) $(VDIS)

# Goals that only read or update the configuration do not need build/pkgs.mk (which needs the buildroot)
ifneq (,$(filter-out help cleantest cleanchroot clean distclean status lock lock-check lock-aarch64 import srcinfo autoupdate sign,$(or $(MAKECMDGOALS),help)))
include $(B)/pkgs.mk
endif

$(B)/pkgs.mk: $(B)/seed-broken.txt scripts/mkpkgs.py scripts/pkg.py scripts/distro.py scripts/srcinfo.py distro.toml distro.lock $(wildcard packages/*/*)
	@mkdir -p $(B)
	python3 scripts/srcinfo.py
	python3 scripts/mkpkgs.py mk >$@

tinyhat32.img: $(B)/vmlinuz $(B)/initramfs.img $(B)/root.sfs $(IMG_DEPS)
	scripts/mkimg.sh $@ $(B)/vmlinuz $(B)/initramfs.img $(B)/root.sfs
	@ls -l $@

$(IMG_RPI4): $(B)/initramfs-rpi4.img $(B)/root-rpi4.sfs $(B)/.buildroot scripts/mkimg-rpi.sh scripts/mkdisk.sh scripts/xfsnormalize.py config/rpi/config.txt config/rpi/cmdline.txt
	scripts/mkimg-rpi.sh $@ $(B)/initramfs-rpi4.img $(B)/root-rpi4.sfs
	@ls -l $@

$(IMG_AARCH64): $(B)/initramfs-aarch64.img $(B)/root-aarch64.sfs $(B)/.buildroot $(B)/.buildroot-aarch64 $(B)/pkg/i686/grub $(B)/pkg/i686/kbd scripts/mkimg-aarch64.sh scripts/mkdisk.sh scripts/xfsnormalize.py scripts/mkgrubtheme.py config/grub.cfg config/grub-early.cfg
	scripts/mkimg-aarch64.sh $@ $(B)/initramfs-aarch64.img $(B)/root-aarch64.sfs
	@ls -l $@

%.vdi: %.img
	rm -f $@.tmp $@.raw
	cp --sparse=always $< $@.raw
	truncate -s $(VDI_SIZE) $@.raw
	VBoxManage convertfromraw $@.raw $@.tmp --format VDI
	rm -f $@.raw
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

# Lock the prebuilt Arch Linux ARM packages for the aarch64 and Raspberry Pi 4 images
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

distbuild: $(IMGS) $(ISOS) $(VDIS)
	DIST_HOME=$(DIST_HOME) DIST_URL=$(DIST_HOME)/releases/download scripts/mkdist.sh $(VERSION) $^
	@ls -l $(B)/dist

sign:
	@test -f $(B)/dist/SHA256SUMS || { echo "make sign: no $(B)/dist/SHA256SUMS, run make distbuild first" >&2; exit 1; }
	gpg --batch --yes --armor --detach-sign $${GPG_KEY:+-u "$$GPG_KEY"} -o $(B)/dist/SHA256SUMS.asc $(B)/dist/SHA256SUMS
	@ls -l $(B)/dist/SHA256SUMS.asc

$(IMG_I686:.img=.iso) $(IMG_X32:.img=.iso) $(IMG_AARCH64:.img=.iso): %.iso: %.img scripts/mkiso.sh config/grub.cfg config/grub-early.cfg
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
	chmod -R u+rwX $(B)/buildroot 2>/dev/null || true
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
	chmod -R u+rwX $(B)/buildroot-aarch64 2>/dev/null || true
	rm -rf $(B)/buildroot-aarch64
	python3 scripts/pkg.py extract $(B)/buildroot-aarch64.lock vendor/pkg-aarch64 $(B)/buildroot-aarch64
	scripts/postinstall.sh $(B)/buildroot-aarch64 $(B)/buildroot-aarch64.lock vendor/pkg-aarch64
	@touch $@

$(B)/seed-broken.txt: $(B)/.buildroot scripts/seedcheck.py
	python3 scripts/seedcheck.py $(B)/buildroot $(B)/buildroot.lock vendor/pkg $@

packages: $(PACKAGES_STAMPS)

x32packages: $(ROOTFS_X32_STAMPS)

aarch64packages: $(AARCH64_STAMPS)

$(B)/rootfs.lock: $(ROOTFS_I686_STAMPS) $(B)/image.toml
	python3 scripts/pkg.py local image $@ $(REPO)

$(B)/rootfs-x32.lock: $(ROOTFS_X32_STAMPS) $(B)/image.toml
	python3 scripts/pkg.py local image $@ $(REPO) x32

$(B)/rootfs-aarch64.lock: $(AARCH64_STAMPS) distro.lock
	python3 scripts/pkg.py archrepo aarch64 $(B)/repo-aarch64 $@ uefi

$(B)/rootfs-rpi4.lock: $(AARCH64_STAMPS) distro.lock
	python3 scripts/pkg.py archrepo aarch64 $(B)/repo-aarch64 $@ rpi4

$(B)/vmlinuz: config/kernel-i686.fragment vendor/src/linux-$(KVER).tar.xz scripts/mkkernel.sh scripts/kcheck.py vendor/src/xpadneo-0.10.4.tar.gz
	@mkdir -p $(B)
	scripts/mkkernel.sh $(KVER) i386 $(B)/vmlinuz $(CURDIR)/config/kernel-i686.fragment

$(B)/vmlinuz-x32: config/kernel-i686.fragment config/kernel-x32.fragment vendor/src/linux-$(KVER).tar.xz scripts/mkkernel.sh scripts/kcheck.py vendor/src/xpadneo-0.10.4.tar.gz
	@mkdir -p $(B)
	scripts/mkkernel.sh $(KVER) x86_64 $(B)/vmlinuz-x32 $(CURDIR)/config/kernel-i686.fragment $(CURDIR)/config/kernel-x32.fragment

$(B)/initramfs.img: $(B)/pkg/i686/busybox $(B)/pkg/i686/tinyhat-splash $(B)/rootfs.lock config/initramfs/init scripts/mkinitramfs.sh
	scripts/mkinitramfs.sh $@

$(B)/initramfs-aarch64.img: $(B)/rootfs-aarch64.lock config/initramfs/init scripts/mkinitramfs.sh
	scripts/mkinitramfs.sh $@ aarch64 linux-aarch64

$(B)/initramfs-rpi4.img: $(B)/rootfs-rpi4.lock config/initramfs/init scripts/mkinitramfs.sh
	scripts/mkinitramfs.sh $@ aarch64 linux-rpi

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

$(B)/.licenses-rpi4: $(B)/rootfs-rpi4.lock $(B)/pkg/i686/licenses $(SOURCES) scripts/mklicenses.py
	python3 scripts/mklicenses.py $(B)/rootfs-rpi4.lock $(B)/licenses-rpi4 aarch64
	@touch $@

ROOTFS_BASE_DEPS := $(SOUNDFONT_GU) vendor/src/pixelitos-icon-theme-20260721.tar.gz scripts/mkpixelitos.py vendor/src/BASS-Floppy-1.3.zip vendor/src/lure-1.1.zip vendor/src/soltys-en-v1.0.zip vendor/src/beej-bgc.zip vendor/src/beej-bgclr.zip vendor/src/open-watcom-2026-10-01.tar.xz $(B)/image.toml $(B)/theme.toml scripts/mkrootfs.sh scripts/mkwallpaper.py scripts/mkhatwallpaper.py scripts/mkhundetann.py scripts/mkarchwallpaper.py img/tinyhat.svg scripts/inroot.sh scripts/mklocaldb.py
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

$(B)/rootfs-rpi4.base.stamp: $(ROOTFS_BASE_DEPS) $(B)/rootfs-rpi4.lock
	@mkdir -p $(B)/modules-rpi4
	TINYHAT_PKGS="$(TINYHAT_PKGS)" scripts/userns.sh scripts/mkrootfs.sh base $(B)/rootfs-rpi4 $(B)/modules-rpi4 $(B)/root-rpi4.sfs $(B)/rootfs-rpi4.lock $(B)/licenses-rpi4
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

$(B)/root-rpi4.sfs: $(B)/rootfs-rpi4.base.stamp $(ROOTFS_FINAL_DEPS) $(B)/.licenses-rpi4
	TINYHAT_PKGS="$(TINYHAT_PKGS)" scripts/userns.sh scripts/mkrootfs.sh finalize $(B)/rootfs-rpi4 $(B)/modules-rpi4 $@ $(B)/rootfs-rpi4.lock $(B)/licenses-rpi4
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

boottest: boottest-i686 boottest-x32 boottest-aarch64 boottest-rpi4

qemutest:
	$(MAKE) -j1 boottest-i686 boottest-x32 boottest-aarch64

boottest-i686: $(IMG_I686)
	python3 scripts/qemutest.py $<

boottest-x32: $(IMG_X32)
	python3 scripts/qemutest.py $<
	python3 scripts/qemutest.py $< --uefi

boottest-aarch64: $(IMG_AARCH64)
	python3 scripts/qemutest.py $<

boottest-rpi4: $(IMG_RPI4)
	python3 scripts/qemutest.py $<

cleantest:
	rm -rf $(B)/qmt $(B)/test-usb.img

cleanchroot:
	scripts/userns.sh rm -rf $(B)/chroot

status:
	@python3 scripts/buildstatus.py $(IMG_I686)
	@python3 scripts/buildstatus.py $(IMG_X32)

clean:
	scripts/userns.sh rm -rf $(B)
	rm -f $(IMGS) $(IMGS:=.tmp) $(ISOS) $(VDIS) $(VDIS:=.tmp)

distclean: clean
	rm -rf vendor/pkg vendor/pkg-aarch64
