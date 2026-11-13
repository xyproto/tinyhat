VERSION := 1.0.7
KVER := 7.3.1

UNAME := $(shell uname -s)

# On macOS every goal is passed on to the build container, so file goals like tinyhat32.img are phony there too
.PHONY: help all img iso ova qemu i686 x32 x86_64 32 64 arm64 aarch64 rpi4 i686_iso x32_iso x86_64_iso aarch64_iso dist distbuild sign packages x32packages aarch64packages x86_64packages fetch sources licenses lock lock-aarch64 lock-x86_64 lock-check autoupdate latest import srcinfo test patchcheck sourcecheck fuzzelcheck run boottest qemutest boottest-i686 boottest-x32 boottest-x86_64 boottest-aarch64 boottest-rpi4 status cleantest cleanchroot clean distclean container $(if $(filter Darwin,$(UNAME)),$(or $(MAKECMDGOALS),help))

ifeq ($(UNAME),Darwin)
PODMAN ?= podman
BUILD_IMAGE ?= tinyhat-build
BUILD_VOLUME ?= tinyhat-build
HOME_VOLUME ?= tinyhat-home
QEMU_MEM ?= 1024
GOALS := $(filter-out run clean distclean,$(or $(MAKECMDGOALS),help))
IN_CONTAINER = $(PODMAN) run --rm --privileged --security-opt label=disable --userns=keep-id \
	-e HOME=/home/builder -v $(HOME_VOLUME):/home/builder \
	-v $(CURDIR):/tinyhat -v $(BUILD_VOLUME):/tinyhat/build -w /tinyhat $(BUILD_IMAGE)

container:
	@$(PODMAN) machine start >/dev/null 2>&1 || true
	$(PODMAN) build -q -t $(BUILD_IMAGE) - <Containerfile >/dev/null
	@for v in $(BUILD_VOLUME) $(HOME_VOLUME); do \
		$(PODMAN) volume exists $$v || { $(PODMAN) volume create $$v >/dev/null && \
		$(PODMAN) run --rm -v $$v:/v $(BUILD_IMAGE) chown $$(id -u):$$(id -g) /v; }; \
	done

ifneq ($(GOALS),)
$(firstword $(GOALS)): container
	$(IN_CONTAINER) make $(GOALS) $(MAKEOVERRIDES)

$(wordlist 2,$(words $(GOALS)),$(GOALS)): $(firstword $(GOALS))
	@:
endif

clean distclean: container
	$(PODMAN) run --rm --privileged --security-opt label=disable --userns=keep-id \
		-v $(CURDIR):/tinyhat -w /tinyhat $(BUILD_IMAGE) make $@
	$(PODMAN) volume rm -f $(BUILD_VOLUME) $(if $(filter distclean,$@),$(HOME_VOLUME)) >/dev/null

run: container
	$(IN_CONTAINER) make 32 $(MAKEOVERRIDES)
	@mkdir -p build
	cp -c tinyhat32.img build/test-usb.img
	dd if=/dev/null of=build/test-usb.img bs=1m seek=4096 2>/dev/null
	qemu-system-i386 -accel tcg,thread=multi -cpu max -smp 4 -m $(QEMU_MEM) \
		-drive if=none,id=stick,file=build/test-usb.img,format=raw \
		-device qemu-xhci -device usb-storage,drive=stick,bootindex=0 \
		-device usb-tablet -vga std \
		-audiodev coreaudio,id=snd -device intel-hda -device hda-duplex,audiodev=snd \
		-nic user,model=e1000 -display cocoa,zoom-to-fit=on
else
export SOURCE_DATE_EPOCH := $(shell git log -1 --format=%ct 2>/dev/null || echo 315532800)
DIST_HOME ?= https://github.com/xyproto/tinyhat
B := build
REPO := $(B)/repo
SOUNDFONT_GU := vendor/soundfont/GeneralUser-GS-2.0.3.sf3
OVERLAY := $(shell find config/overlay)
SOURCES := $(addprefix vendor/src/,$(shell python3 scripts/distro.py sources | awk '{ print $$1 }'))
QEMU_MEM ?= 1024
DISK_SIZE ?= 20G

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
IMG_X86_64 := tinyhat64.img
IMG_AARCH64 := tinyhat-aarch64.img
IMG_RPI4 := tinyhat-rpi4.img
IMGS := $(IMG_I686) $(IMG_X32) $(IMG_X86_64) $(IMG_AARCH64) $(IMG_RPI4)
ISOS := $(IMG_I686:.img=.iso) $(IMG_X32:.img=.iso) $(IMG_X86_64:.img=.iso) $(IMG_AARCH64:.img=.iso)
X86_IMGS := $(IMG_I686) $(IMG_X32) $(IMG_X86_64)
QEMU_TARS := $(IMG_AARCH64:.img=.tar.gz)
OVAS := $(X86_IMGS:.img=.ova)
X86_FILES := $(X86_IMGS) $(X86_IMGS:.img=.iso) $(OVAS)
RUST_DIST := vendor/src/rust-1.99.1-i686-unknown-linux-gnu.tar.xz
KERNEL_DEPS := vendor/src/linux-$(KVER).tar.xz vendor/src/xpadneo-0.10.5.tar.gz scripts/mkkernel.sh scripts/kcheck.py config/kernel-common.fragment config/kernel-tinyhat-panic.patch
MKROOTFS = TINYHAT_PKGS="$(TINYHAT_PKGS)" TINYHAT_VERSION="$(VERSION)" scripts/userns.sh scripts/mkrootfs.sh
$(shell mkdir -p $(B) && [ "$$(cat $(B)/version 2>/dev/null)" = "$(VERSION)" ] || echo "$(VERSION)" > $(B)/version)
MKDIST := DIST_HOME=$(DIST_HOME) DIST_URL=$(DIST_HOME)/releases/download scripts/mkdist.sh $(VERSION)

.DELETE_ON_ERROR:
.DEFAULT_GOAL := help

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
	$(call target,64,"64-bit x86 image ($(IMG_X86_64))$(comma) with Arch Linux packages (make x86_64 also works)")
	$(call target,x32,"64-bit x86 with 32-bit pointers image ($(IMG_X32))")
	$(call target,arm64,"64-bit ARM UEFI image ($(IMG_AARCH64)) (make aarch64 also works)")
	$(call target,rpi4,"64-bit ARM Raspberry Pi 4 image ($(IMG_RPI4))")
	@printf '\n$(C_GRP)CD/DVD/USB ISO images$(C_OFF)\n'
	$(call target,i686_iso,"32-bit x86 ISO")
	$(call target,x32_iso,"x32 ISO")
	$(call target,x86_64_iso,"64-bit x86 ISO")
	$(call target,aarch64_iso,"64-bit ARM UEFI ISO")
	$(call target,iso,"all four ISO images")
	@printf '\n$(C_GRP)Groups and releases$(C_OFF)\n'
	$(call target,ova,"VirtualBox appliances (i686$(comma) x32$(comma) x86_64)")
	$(call target,qemu,"aarch64 QEMU disk image and run script (.tar.gz)")
	$(call target,all,"every .img$(comma) .iso$(comma) .ova and .tar.gz")
	$(call target,dist,"release files in $(B)/dist$(comma) signed")
	@printf '\n$(C_GRP)Packages and maintenance$(C_OFF)\n'
	$(call target,packages,"build or fetch every package the images need")
	$(call target,lock,"update distro.lock")
	$(call target,lock-check,"check that distro.lock is up to date")
	$(call target,lock-x86_64,"lock the prebuilt Arch Linux packages for the x86_64 image")
	$(call target,autoupdate,"update recipes to newer versions")
	$(call target,latest,"list recipes that are behind upstream (pkgctl version check)")
	$(call target,test,"run the checks")
	$(call target,boottest,"boot every image headless in QEMU (also boottest-i686 and so on)")
	$(call target,qemutest,"boot the i686$(comma) x32$(comma) x86_64 and aarch64 images in QEMU$(comma) one at a time")
	$(call target,run,"boot the i686 image in QEMU")
	$(call target,status,"show what still needs building")
	$(call target,clean,"remove build output")
	@printf '\n'

i686: $(IMG_I686)

x32: $(IMG_X32)

x86_64: $(IMG_X86_64)

32: i686

64: x86_64

arm64: aarch64

aarch64: $(IMG_AARCH64)

rpi4: $(IMG_RPI4)

i686_iso: $(IMG_I686:.img=.iso)

x32_iso: $(IMG_X32:.img=.iso)

x86_64_iso: $(IMG_X86_64:.img=.iso)

aarch64_iso: $(IMG_AARCH64:.img=.iso)

img: $(IMGS)

iso: $(ISOS)

ova: $(OVAS)

qemu: $(QEMU_TARS)

all: $(IMGS) $(ISOS) $(OVAS) $(QEMU_TARS)

# Goals that only read or update the configuration do not need build/pkgs.mk (which needs the buildroot)
ifneq (,$(filter-out help cleantest cleanchroot clean distclean status lock lock-check lock-aarch64 lock-x86_64 import srcinfo autoupdate latest sign,$(or $(MAKECMDGOALS),help)))
include $(B)/pkgs.mk
endif

$(B)/pkgs.mk: $(B)/seed-broken.txt scripts/mkpkgs.py scripts/pkg.py scripts/distro.py scripts/srcinfo.py distro.toml distro.lock $(wildcard packages/*/*)
	@mkdir -p $(B)
	python3 scripts/srcinfo.py
	python3 scripts/mkpkgs.py mk >$@

$(IMG_I686): $(B)/vmlinuz $(B)/initramfs.img $(B)/root.sfs $(IMG_DEPS)
	scripts/mkimg.sh $@ $(B)/vmlinuz $(B)/initramfs.img $(B)/root.sfs
	@ls -l $@

$(IMG_RPI4): $(B)/Image-arm64 $(B)/initramfs-aarch64.img $(B)/root-rpi4.sfs $(B)/.buildroot scripts/mkimg-rpi.sh scripts/mkdisk.sh scripts/xfsnormalize.py config/rpi/config.txt config/rpi/cmdline.txt
	scripts/mkimg-rpi.sh $@ $(B)/initramfs-aarch64.img $(B)/root-rpi4.sfs $(B)/Image-arm64
	@ls -l $@

$(IMG_AARCH64): $(B)/Image-arm64 $(B)/initramfs-aarch64.img $(B)/root-aarch64.sfs $(B)/.buildroot $(B)/.buildroot-aarch64 $(B)/pkg/i686/grub $(B)/pkg/i686/kbd scripts/mkimg-aarch64.sh scripts/mkdisk.sh scripts/xfsnormalize.py scripts/mkgrubtheme.py config/grub.cfg config/grub-early.cfg
	scripts/mkimg-aarch64.sh $@ $(B)/initramfs-aarch64.img $(B)/root-aarch64.sfs $(B)/Image-arm64
	@ls -l $@

%.ova: %.img scripts/mkova.sh
	scripts/mkova.sh $< $@ $(if $(filter $(IMG_I686),$<),ArchLinux,ArchLinux_64) $(VERSION) $(DISK_SIZE)
	@ls -l $@

%.tar.gz: %.img scripts/mkqemutar.sh
	scripts/mkqemutar.sh $< $@ $(DISK_SIZE)
	@ls -l $@

$(IMG_X32): $(B)/vmlinuz-x32 $(B)/initramfs.img $(B)/root64.sfs $(IMG_DEPS) $(B)/pkg/i686/grub-efi64 $(B)/pkg/i686/sbsigntools vendor/src/shim-x64-16.1-10.x86_64.rpm $(SB_KEYS)/tinyhat.cer
	scripts/mkimg.sh $@ $(B)/vmlinuz-x32 $(B)/initramfs.img $(B)/root64.sfs efi64 $(SB_KEYS)
	@ls -l $@

# The 64-bit x86 image boots the kernel and initramfs of the x32 image, with packages from Arch Linux
$(IMG_X86_64): $(B)/vmlinuz-x32 $(B)/initramfs.img $(B)/root-x86_64.sfs $(IMG_DEPS) $(B)/pkg/i686/grub-efi64 $(B)/pkg/i686/sbsigntools vendor/src/shim-x64-16.1-10.x86_64.rpm $(SB_KEYS)/tinyhat.cer
	scripts/mkimg.sh $@ $(B)/vmlinuz-x32 $(B)/initramfs.img $(B)/root-x86_64.sfs efi64 $(SB_KEYS)
	@ls -l $@

$(SB_KEYS)/tinyhat.cer:
	scripts/mksbkey.sh $(SB_KEYS)

lock:
	@mkdir -p $(B)
	python3 scripts/srcinfo.py
	python3 scripts/mkpkgs.py seed >$(B)/seed.txt
	python3 scripts/mkpkgs.py libs
	python3 scripts/pkg.py lock buildroot $(B)/seed.txt

# Lock the prebuilt Arch Linux ARM packages for the aarch64 and rpi4 images
lock-aarch64:
	python3 scripts/srcinfo.py
	python3 scripts/mkpkgs.py lock-arch aarch64

# Lock the prebuilt Arch Linux packages for the x86_64 image
lock-x86_64:
	python3 scripts/srcinfo.py
	python3 scripts/mkpkgs.py lock-arch x86_64

lock-check:
	@mkdir -p $(B)
	@cp distro.lock $(B)/distro.lock.before
	@$(MAKE) -s lock || { cp $(B)/distro.lock.before distro.lock; exit 1; }
	@if cmp -s distro.lock $(B)/distro.lock.before; then echo "distro.lock is up to date"; \
	else cp $(B)/distro.lock.before distro.lock; echo "distro.lock is stale, run make lock" >&2; exit 1; fi

$(B)/theme.toml $(B)/image.toml: $(B)/%.toml: $(B)/.%.toml.stamp ;

$(B)/.theme.toml.stamp $(B)/.image.toml.stamp: $(B)/.%.toml.stamp: distro.toml scripts/distro.py
	@mkdir -p $(B)
	python3 scripts/distro.py view $* $(B)/$*.toml
	@touch $@

$(B)/buildroot.lock: distro.lock scripts/distro.py
	@mkdir -p $(B)
	python3 scripts/distro.py lockfile buildroot $@

$(B)/pkg/i686/tinyhat-splash: $(B)/theme.toml

import:
	python3 scripts/importpkg.py all

# Update recipes and the lock to newer package versions, holding back updates that hit [image] deny
autoupdate:
	python3 scripts/autoupdate.py

latest:
	python3 scripts/latest.py

srcinfo:
	python3 scripts/srcinfo.py $(if $(ALL),packages/*/)

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

distbuild: $(IMGS) $(ISOS) $(OVAS) $(QEMU_TARS)
distbuild:
	$(MKDIST) $^
	@ls -l $(B)/dist

sign:
	@test -f $(B)/dist/SHA256SUMS || { echo "make sign: no $(B)/dist/SHA256SUMS, run make distbuild first" >&2; exit 1; }
	gpg --batch --yes --armor --detach-sign $${GPG_KEY:+-u "$$GPG_KEY"} -o $(B)/dist/SHA256SUMS.asc $(B)/dist/SHA256SUMS
	@ls -l $(B)/dist/SHA256SUMS.asc

$(ISOS): %.iso: %.img scripts/mkiso.sh config/grub.cfg config/grub-early.cfg
	scripts/mkiso.sh $< $@

fetch: $(B)/.fetched-buildroot sources

sources: $(SOURCES)

vendor/src/%:
	python3 scripts/pkg.py source vendor/src $*

$(B)/.fetched-buildroot: $(B)/buildroot.lock
	@mkdir -p $(B)
	python3 scripts/pkg.py fetch $< vendor/pkg
	@touch $@

BUILDROOT_LOCAL := $(shell python3 scripts/distro.py local buildroot)

$(B)/.buildroot: $(B)/.fetched-buildroot scripts/postinstall.sh scripts/rust.sh scripts/localroot.py $(RUST_DIST) $(foreach p,$(BUILDROOT_LOCAL),$(filter-out %/tiny.lua %/.SRCINFO,$(wildcard packages/$(p)/*)))
	chmod -R u+rwX $(B)/buildroot 2>/dev/null || true
	rm -rf $(B)/buildroot
	python3 scripts/pkg.py extract $(B)/buildroot.lock vendor/pkg $(B)/buildroot buildroot
	ldconfig -r $(B)/buildroot
	scripts/postinstall.sh $(B)/buildroot $(B)/buildroot.lock vendor/pkg
	scripts/rust.sh $(B)/buildroot $(RUST_DIST)
	python3 scripts/localroot.py $(B)/buildroot
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

$(B)/buildroot-x86_64.lock: distro.lock scripts/distro.py
	@mkdir -p $(B)
	python3 scripts/distro.py lockfile buildroot-x86_64 $@

$(B)/.fetched-buildroot-x86_64: $(B)/buildroot-x86_64.lock
	python3 scripts/pkg.py fetch $< vendor/pkg-x86_64 x86_64
	@touch $@

$(B)/.buildroot-x86_64: $(B)/.fetched-buildroot-x86_64 scripts/postinstall.sh
	chmod -R u+rwX $(B)/buildroot-x86_64 2>/dev/null || true
	rm -rf $(B)/buildroot-x86_64
	python3 scripts/pkg.py extract $(B)/buildroot-x86_64.lock vendor/pkg-x86_64 $(B)/buildroot-x86_64
	scripts/postinstall.sh $(B)/buildroot-x86_64 $(B)/buildroot-x86_64.lock vendor/pkg-x86_64
	@touch $@

$(B)/seed-broken.txt: $(B)/.buildroot scripts/seedcheck.py
	python3 scripts/seedcheck.py $(B)/buildroot $(B)/buildroot.lock vendor/pkg $@

packages: $(PACKAGES_STAMPS)

x32packages: $(ROOTFS_X32_STAMPS)

aarch64packages: $(AARCH64_STAMPS)

x86_64packages: $(X86_64_STAMPS)

$(B)/rootfs.lock: $(ROOTFS_I686_STAMPS) $(B)/image.toml
	python3 scripts/pkg.py local image $@ $(REPO)

$(B)/rootfs-x32.lock: $(ROOTFS_X32_STAMPS) $(B)/image.toml
	python3 scripts/pkg.py local image $@ $(REPO) x32

$(B)/rootfs-aarch64.lock: $(AARCH64_STAMPS) distro.lock
	python3 scripts/pkg.py archrepo aarch64 $(B)/repo-aarch64 $@ uefi

$(B)/rootfs-x86_64.lock: $(X86_64_STAMPS) distro.lock
	python3 scripts/pkg.py archrepo x86_64 $(B)/repo-x86_64 $@

$(B)/rootfs-rpi4.lock: $(AARCH64_STAMPS) distro.lock
	python3 scripts/pkg.py archrepo aarch64 $(B)/repo-aarch64 $@ rpi4

$(B)/vmlinuz: $(KERNEL_DEPS) config/kernel-x86.fragment config/kernel-i686.fragment
	@mkdir -p $(B)
	scripts/mkkernel.sh $(KVER) i386 $(B)/vmlinuz $(CURDIR)/config/kernel-common.fragment $(CURDIR)/config/kernel-x86.fragment $(CURDIR)/config/kernel-i686.fragment

$(B)/vmlinuz-x32: $(KERNEL_DEPS) config/kernel-x86.fragment config/kernel-x32.fragment
	@mkdir -p $(B)
	scripts/mkkernel.sh $(KVER) x86_64 $(B)/vmlinuz-x32 $(CURDIR)/config/kernel-common.fragment $(CURDIR)/config/kernel-x86.fragment $(CURDIR)/config/kernel-x32.fragment

$(B)/Image-arm64: $(KERNEL_DEPS) config/kernel-arm64.fragment
	@mkdir -p $(B)
	scripts/mkkernel.sh $(KVER) arm64 $(B)/Image-arm64 $(CURDIR)/config/kernel-common.fragment $(CURDIR)/config/kernel-arm64.fragment

$(B)/initramfs.img: $(B)/pkg/i686/busybox $(B)/pkg/i686/tinyhat-splash $(B)/pkg/i686/tinyhat-guru $(B)/rootfs.lock config/initramfs/init scripts/mkinitramfs.sh
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

$(B)/.licenses-x86_64: $(B)/rootfs-x86_64.lock $(LICENSE_DEPS) $(B)/pkg/i686/grub-efi64
	python3 scripts/mklicenses.py $(B)/rootfs-x86_64.lock $(B)/licenses-x86_64 x86_64
	@touch $@

$(B)/.licenses-aarch64: $(B)/rootfs-aarch64.lock $(B)/pkg/i686/licenses $(SOURCES) scripts/mklicenses.py
	python3 scripts/mklicenses.py $(B)/rootfs-aarch64.lock $(B)/licenses-aarch64 aarch64
	@touch $@

$(B)/.licenses-rpi4: $(B)/rootfs-rpi4.lock $(B)/pkg/i686/licenses $(SOURCES) scripts/mklicenses.py
	python3 scripts/mklicenses.py $(B)/rootfs-rpi4.lock $(B)/licenses-rpi4 aarch64
	@touch $@

ROOTFS_BASE_DEPS := $(SOUNDFONT_GU) vendor/src/pixelitos-icon-theme-20260721.tar.gz scripts/mkpixelitos.py scripts/mkfennelbook.py vendor/src/BASS-Floppy-1.3.zip vendor/src/soltys-en-v1.0.zip vendor/src/beej-bgc.zip vendor/src/beej-bgclr.zip vendor/src/open-watcom-2026-10-01.tar.xz $(B)/image.toml $(B)/theme.toml scripts/mkrootfs.sh scripts/mkwallpaper.py scripts/mkhatwallpaper.py scripts/mkhundetann.py scripts/mkarchwallpaper.py img/tinyhat.svg scripts/inroot.sh scripts/mklocaldb.py scripts/fwprune.py scripts/elfshrink.py
ROOTFS_FINAL_DEPS := $(B)/version $(B)/theme.toml $(OVERLAY) scripts/forbidcheck.py scripts/sonamecheck.py scripts/elfcheck.py scripts/symcheck.sh scripts/elfshrink.py

$(B)/rootfs.base.stamp: $(ROOTFS_BASE_DEPS) $(B)/rootfs.lock $(B)/vmlinuz
	$(MKROOTFS) base $(B)/rootfs $(B)/modules $(B)/root.sfs $(B)/rootfs.lock $(B)/licenses
	touch $@

$(B)/rootfs-x32.base.stamp: $(ROOTFS_BASE_DEPS) $(B)/rootfs-x32.lock $(B)/vmlinuz-x32
	$(MKROOTFS) base $(B)/rootfs64 $(B)/modules-x86_64 $(B)/root64.sfs $(B)/rootfs-x32.lock $(B)/licenses-x32
	touch $@

$(B)/rootfs-x86_64.base.stamp: $(ROOTFS_BASE_DEPS) $(B)/rootfs-x86_64.lock $(B)/vmlinuz-x32
	$(MKROOTFS) base $(B)/rootfs-x86_64 $(B)/modules-x86_64 $(B)/root-x86_64.sfs $(B)/rootfs-x86_64.lock $(B)/licenses-x86_64
	touch $@

$(B)/rootfs-aarch64.base.stamp: $(ROOTFS_BASE_DEPS) $(B)/rootfs-aarch64.lock $(B)/Image-arm64
	$(MKROOTFS) base $(B)/rootfs-aarch64 $(B)/modules-arm64 $(B)/root-aarch64.sfs $(B)/rootfs-aarch64.lock $(B)/licenses-aarch64
	touch $@

$(B)/rootfs-rpi4.base.stamp: $(ROOTFS_BASE_DEPS) $(B)/rootfs-rpi4.lock $(B)/Image-arm64
	$(MKROOTFS) base $(B)/rootfs-rpi4 $(B)/modules-arm64 $(B)/root-rpi4.sfs $(B)/rootfs-rpi4.lock $(B)/licenses-rpi4
	touch $@

$(B)/root.sfs: $(B)/rootfs.base.stamp $(ROOTFS_FINAL_DEPS) $(B)/.licenses-i686
	$(MKROOTFS) finalize $(B)/rootfs $(B)/modules $@ $(B)/rootfs.lock $(B)/licenses
	@ls -l $@

$(B)/root64.sfs: $(B)/rootfs-x32.base.stamp $(ROOTFS_FINAL_DEPS) $(B)/.licenses-x32
	$(MKROOTFS) finalize $(B)/rootfs64 $(B)/modules-x86_64 $@ $(B)/rootfs-x32.lock $(B)/licenses-x32
	@ls -l $@

$(B)/root-x86_64.sfs: $(B)/rootfs-x86_64.base.stamp $(ROOTFS_FINAL_DEPS) $(B)/.licenses-x86_64
	$(MKROOTFS) finalize $(B)/rootfs-x86_64 $(B)/modules-x86_64 $@ $(B)/rootfs-x86_64.lock $(B)/licenses-x86_64
	@ls -l $@

$(B)/root-aarch64.sfs: $(B)/rootfs-aarch64.base.stamp $(ROOTFS_FINAL_DEPS) $(B)/.licenses-aarch64
	$(MKROOTFS) finalize $(B)/rootfs-aarch64 $(B)/modules-arm64 $@ $(B)/rootfs-aarch64.lock $(B)/licenses-aarch64
	@ls -l $@

$(B)/root-rpi4.sfs: $(B)/rootfs-rpi4.base.stamp $(ROOTFS_FINAL_DEPS) $(B)/.licenses-rpi4
	$(MKROOTFS) finalize $(B)/rootfs-rpi4 $(B)/modules-arm64 $@ $(B)/rootfs-rpi4.lock $(B)/licenses-rpi4
	@ls -l $@

$(B)/test-usb.img: $(IMG_I686)
	cp --sparse=always $< $@
	truncate -s 4G $@

run: $(B)/test-usb.img
	qemu-system-i386 -enable-kvm -cpu host -smp 2 -m $(QEMU_MEM) \
		-drive if=none,id=stick,file=$(B)/test-usb.img,format=raw \
		-device qemu-xhci -device usb-storage,drive=stick,bootindex=0 \
		-device usb-tablet -vga std \
		-audiodev pa,id=snd -device intel-hda -device hda-duplex,audiodev=snd \
		-nic user,model=e1000

boottest: boottest-i686 boottest-x32 boottest-x86_64 boottest-aarch64 boottest-rpi4

qemutest:
	$(MAKE) -j1 boottest-i686 boottest-x32 boottest-x86_64 boottest-aarch64

boottest-i686: $(IMG_I686)
	python3 scripts/qemutest.py $<

boottest-x32: $(IMG_X32)
	python3 scripts/qemutest.py $<
	python3 scripts/qemutest.py $< --uefi

boottest-x86_64: $(IMG_X86_64)
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
	rm -f $(IMGS) $(IMGS:=.tmp) $(ISOS) $(QEMU_TARS) $(QEMU_TARS:=.tmp) $(OVAS) $(OVAS:=.tmp.ova)

distclean: clean
	rm -rf vendor/pkg vendor/pkg-aarch64 vendor/pkg-x86_64

endif
