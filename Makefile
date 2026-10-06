KVER := 7.2.9
B := build
LOCKS := rootfs.lock buildroot.lock initramfs.lock licenses.lock
SOUNDFONT := vendor/soundfont/TinyHat-GM-0.790.sf3
SOUNDFONT_GU := vendor/soundfont/GeneralUser-GS-2.0.3.sf3
OVERLAY := $(shell find config/overlay)
SOURCES := $(addprefix vendor/src/,$(shell awk '!/^#/ { print $$1 }' sources.txt))
QEMU_MEM ?= 1024

.DELETE_ON_ERROR:
.PHONY: all lock fetch sources licenses run clean distclean tinyhat64.img

all: tinyhat32.img

tinyhat32.img: $(B)/vmlinuz $(B)/initramfs.img $(B)/root.sfs $(B)/.fetched-initramfs config/grub.cfg config/grub-early.cfg scripts/mkimg.sh scripts/mbr.py
	scripts/mkimg.sh $@ $(B)/vmlinuz $(B)/initramfs.img $(B)/root.sfs
	@ls -l $@

tinyhat64.img:
	@echo "tinyhat64.img is not supported yet" >&2
	@false

lock:
	REFRESH=1 python3 scripts/pkg.py lock config/rootfs.txt rootfs.lock
	python3 scripts/pkg.py lock config/buildroot.txt buildroot.lock
	python3 scripts/pkg.py lock config/initramfs.txt initramfs.lock
	python3 scripts/pkg.py lock config/licenses.txt licenses.lock

fetch: $(LOCKS:%.lock=$(B)/.fetched-%) sources

sources: $(SOURCES)

vendor/src/%:
	python3 scripts/pkg.py source sources.txt vendor/src $*

$(B)/.fetched-%: %.lock
	@mkdir -p $(B)
	python3 scripts/pkg.py fetch $< vendor/pkg
	@touch $@

$(B)/.buildroot: $(B)/.fetched-buildroot
	chmod -R u+w $(B)/buildroot 2>/dev/null || true
	rm -rf $(B)/buildroot
	python3 scripts/pkg.py extract buildroot.lock vendor/pkg $(B)/buildroot
	ldconfig -r $(B)/buildroot
	@touch $@

$(B)/.sdl3: $(B)/.buildroot recipes/sdl3.sh vendor/src/SDL3-3.4.18.tar.gz $(wildcard vendor/patches/sdl3/*)
	scripts/recipe.sh $(B)/buildroot sdl3 >$(B)/sdl3.log 2>&1 || { tail -40 $(B)/sdl3.log; exit 1; }
	@touch $@

$(B)/.zsnes: $(B)/.sdl3 recipes/zsnes.sh vendor/src/zsnes-2.3.6.tar.gz
	scripts/recipe.sh $(B)/buildroot zsnes >$(B)/zsnes.log 2>&1 || { tail -40 $(B)/zsnes.log; exit 1; }
	@touch $@

$(B)/.sdl2-compat: $(B)/.sdl3 recipes/sdl2-compat.sh vendor/src/sdl2-compat-2.32.74.tar.gz
	scripts/recipe.sh $(B)/buildroot sdl2-compat >$(B)/sdl2-compat.log 2>&1 || { tail -40 $(B)/sdl2-compat.log; exit 1; }
	@touch $@

$(B)/.dosbox-x: $(B)/.sdl2-compat $(B)/.fluidsynth recipes/dosbox-x.sh vendor/src/dosbox-x-2026.10.01.tar.gz $(wildcard vendor/patches/dosbox-x/*)
	scripts/recipe.sh $(B)/buildroot dosbox-x >$(B)/dosbox-x.log 2>&1 || { tail -40 $(B)/dosbox-x.log; exit 1; }
	@touch $@

$(B)/.libdisplay-info: $(B)/.buildroot recipes/libdisplay-info.sh vendor/src/libdisplay-info-0.3.0.tar.gz
	scripts/recipe.sh $(B)/buildroot libdisplay-info >$(B)/libdisplay-info.log 2>&1 || { tail -40 $(B)/libdisplay-info.log; exit 1; }
	@touch $@

$(B)/.wlroots: $(B)/.libdisplay-info recipes/wlroots.sh vendor/src/wlroots-0.19.3.tar.gz
	scripts/recipe.sh $(B)/buildroot wlroots >$(B)/wlroots.log 2>&1 || { tail -40 $(B)/wlroots.log; exit 1; }
	@touch $@

$(B)/.dwl: $(B)/.wlroots recipes/dwl.sh vendor/src/dwl-0.8.tar.gz $(wildcard vendor/patches/dwl/*)
	scripts/recipe.sh $(B)/buildroot dwl >$(B)/dwl.log 2>&1 || { tail -40 $(B)/dwl.log; exit 1; }
	@touch $@

$(B)/.fuzzel: $(B)/.buildroot recipes/fuzzel.sh vendor/src/fuzzel-1.8.1.tar.gz
	scripts/recipe.sh $(B)/buildroot fuzzel >$(B)/fuzzel.log 2>&1 || { tail -40 $(B)/fuzzel.log; exit 1; }
	@touch $@

$(B)/.swaylock: $(B)/.buildroot recipes/swaylock.sh vendor/src/swaylock-1.8.6.tar.gz
	scripts/recipe.sh $(B)/buildroot swaylock >$(B)/swaylock.log 2>&1 || { tail -40 $(B)/swaylock.log; exit 1; }
	@touch $@

$(B)/.orbiton: $(B)/.buildroot recipes/orbiton.sh vendor/src/go1.26.8.linux-386.tar.gz vendor/src/orbiton-4bdfcf9f502df6ff19b7c33ea53233eab6d4653c.tar.gz $(wildcard vendor/patches/orbiton/*)
	scripts/recipe.sh $(B)/buildroot orbiton >$(B)/orbiton.log 2>&1 || { tail -40 $(B)/orbiton.log; exit 1; }
	@touch $@

$(B)/.sdl2_image: $(B)/.sdl2-compat recipes/sdl2_image.sh vendor/src/SDL2_image-2.8.12.tar.gz
	scripts/recipe.sh $(B)/buildroot sdl2_image >$(B)/sdl2_image.log 2>&1 || { tail -40 $(B)/sdl2_image.log; exit 1; }
	@touch $@

$(B)/.grafx2: $(B)/.sdl3_image $(B)/.sdl3_ttf recipes/grafx2.sh vendor/src/grafx2-f84cb09dc59d706d6e7b28778b01ba911f52298c.tar.gz vendor/src/lua-5.4.9.tar.gz vendor/src/recoil-6.4.5.tar.gz vendor/src/6502-v0.1.tar.xz $(wildcard vendor/patches/grafx2/*)
	scripts/recipe.sh $(B)/buildroot grafx2 >$(B)/grafx2.log 2>&1 || { tail -40 $(B)/grafx2.log; exit 1; }
	@touch $@

$(B)/.wordgrinder: $(B)/.buildroot recipes/wordgrinder.sh vendor/src/wordgrinder-0.8.tar.gz
	scripts/recipe.sh $(B)/buildroot wordgrinder >$(B)/wordgrinder.log 2>&1 || { tail -40 $(B)/wordgrinder.log; exit 1; }
	@touch $@

$(B)/.sdl3_image: $(B)/.sdl3 recipes/sdl3_image.sh vendor/src/SDL3_image-3.4.8.tar.gz
	scripts/recipe.sh $(B)/buildroot sdl3_image >$(B)/sdl3_image.log 2>&1 || { tail -40 $(B)/sdl3_image.log; exit 1; }
	@touch $@

$(B)/.sdl3_ttf: $(B)/.sdl3 recipes/sdl3_ttf.sh vendor/src/SDL3_ttf-3.2.2.tar.gz
	scripts/recipe.sh $(B)/buildroot sdl3_ttf >$(B)/sdl3_ttf.log 2>&1 || { tail -40 $(B)/sdl3_ttf.log; exit 1; }
	@touch $@

$(B)/.raylib: $(B)/.sdl3 recipes/raylib.sh vendor/src/raylib-6.0.tar.gz
	scripts/recipe.sh $(B)/buildroot raylib >$(B)/raylib.log 2>&1 || { tail -40 $(B)/raylib.log; exit 1; }
	@touch $@

$(B)/.sdl3-man: $(B)/.buildroot recipes/sdl3-man.sh vendor/src/SDL3-3.4.18.tar.gz vendor/src/SDL3_image-3.4.8.tar.gz vendor/src/SDL3_ttf-3.2.2.tar.gz
	scripts/recipe.sh $(B)/buildroot sdl3-man >$(B)/sdl3-man.log 2>&1 || { tail -40 $(B)/sdl3-man.log; exit 1; }
	@touch $@

$(B)/.fontconfig: $(B)/.buildroot recipes/fontconfig.sh vendor/src/fontconfig-2.18.3.tar.xz vendor/src/meson-1.12.1.tar.gz
	scripts/recipe.sh $(B)/buildroot fontconfig >$(B)/fontconfig.log 2>&1 || { tail -40 $(B)/fontconfig.log; exit 1; }
	@touch $@

$(B)/.swaybg: $(B)/.buildroot recipes/swaybg.sh vendor/src/swaybg-1.2.2.tar.gz $(wildcard vendor/patches/swaybg/*)
	scripts/recipe.sh $(B)/buildroot swaybg >$(B)/swaybg.log 2>&1 || { tail -40 $(B)/swaybg.log; exit 1; }
	@touch $@

$(B)/.libxml2: $(B)/.buildroot recipes/libxml2.sh vendor/src/libxml2-2.15.1.tar.xz
	scripts/recipe.sh $(B)/buildroot libxml2 >$(B)/libxml2.log 2>&1 || { tail -40 $(B)/libxml2.log; exit 1; }
	@touch $@

$(B)/.libxml2-legacy: $(B)/.buildroot recipes/libxml2-legacy.sh vendor/src/libxml2-2.13.9.tar.xz
	scripts/recipe.sh $(B)/buildroot libxml2-legacy >$(B)/libxml2-legacy.log 2>&1 || { tail -40 $(B)/libxml2-legacy.log; exit 1; }
	@touch $@

$(B)/.cc65: $(B)/.buildroot recipes/cc65.sh vendor/src/cc65-71746c829e77f74c2b601a7d16821170c2610df3.tar.gz
	scripts/recipe.sh $(B)/buildroot cc65 >$(B)/cc65.log 2>&1 || { tail -40 $(B)/cc65.log; exit 1; }
	@touch $@

$(B)/.gdb: $(B)/.buildroot recipes/gdb.sh vendor/src/gdb-17.1.tar.xz
	scripts/recipe.sh $(B)/buildroot gdb >$(B)/gdb.log 2>&1 || { tail -40 $(B)/gdb.log; exit 1; }
	@touch $@

$(B)/.fluidsynth: $(B)/.buildroot recipes/fluidsynth.sh vendor/src/fluidsynth-2.6.1.tar.gz vendor/src/gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200.zip
	scripts/recipe.sh $(B)/buildroot fluidsynth >$(B)/fluidsynth.log 2>&1 || { tail -40 $(B)/fluidsynth.log; exit 1; }
	@touch $@

$(B)/.scummvm: $(B)/.sdl3 $(B)/.fluidsynth recipes/scummvm.sh vendor/src/scummvm-2026.3.0.tar.xz $(wildcard vendor/patches/scummvm/*)
	scripts/recipe.sh $(B)/buildroot scummvm >$(B)/scummvm.log 2>&1 || { tail -40 $(B)/scummvm.log; exit 1; }
	@touch $@

$(B)/vmlinuz: config/kernel-i686.fragment vendor/src/linux-$(KVER).tar.xz scripts/mkkernel.sh scripts/kcheck.py vendor/src/xpadneo-0.10.4.tar.gz
	@mkdir -p $(B)
	scripts/mkkernel.sh $(KVER) $(CURDIR)/config/kernel-i686.fragment

$(B)/initramfs.img: $(B)/.fetched-initramfs config/initramfs/init scripts/mkinitramfs.sh
	scripts/mkinitramfs.sh $@

$(SOUNDFONT): | vendor/src/FatBoy-v0.790.7z
	@mkdir -p $(B)/fatboy $(dir $@)
	7z x -y -o$(B)/fatboy vendor/src/FatBoy-v0.790.7z >/dev/null
	python3 scripts/sf2tosf3.py $(B)/fatboy/FatBoy-v0.790.sf2 $@ "TinyHat-GM 0.790" \
		"TinyHat-GM is an SF3 (Ogg Vorbis) conversion of FatBoy 0.790 by Chris Wadge and contributors, renamed as the FatBoy license asks for modified versions. Samples were normalized and the instrument attenuation adjusted to keep the original balance."
	rm -rf $(B)/fatboy

$(SOUNDFONT_GU): | vendor/src/GeneralUser-GS-2.0.3.sf2
	@mkdir -p $(dir $@)
	python3 scripts/sf2tosf3.py vendor/src/GeneralUser-GS-2.0.3.sf2 $@

licenses: $(B)/.licenses

$(B)/.licenses: $(LOCKS:%.lock=$(B)/.fetched-%) $(SOURCES) scripts/mklicenses.py
	python3 scripts/mklicenses.py
	@touch $@

$(B)/root.sfs: $(B)/.fetched-rootfs $(B)/.wordgrinder $(B)/.grafx2 $(B)/.sdl2_image $(B)/.gdb $(B)/.cc65 $(B)/.zsnes $(B)/.dosbox-x $(B)/.scummvm $(B)/.dwl $(B)/.fuzzel $(B)/.swaylock $(B)/.swaybg $(B)/.orbiton $(B)/.raylib $(B)/.sdl3-man $(B)/.fontconfig $(B)/.libxml2 $(B)/.libxml2-legacy $(B)/.fluidsynth $(B)/vmlinuz $(B)/.licenses $(SOUNDFONT) $(SOUNDFONT_GU) vendor/src/FOTAQ_Talkie-1.1.zip vendor/src/beej-bgc.zip vendor/src/beej-bgclr.zip vendor/src/open-watcom-2026-10-01.tar.xz $(filter vendor/src/lib%-i686.pkg.tar.zst,$(SOURCES)) $(OVERLAY) config/rootfs.exclude scripts/mkrootfs.sh scripts/inroot.sh scripts/elfcheck.py scripts/symcheck.sh scripts/mklocaldb.py scripts/mklocalpkg.py
	scripts/userns.sh scripts/mkrootfs.sh $(B)/rootfs $(B)/modules $@
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

clean:
	scripts/userns.sh rm -rf $(B)
	rm -f tinyhat32.img tinyhat32.img.tmp

distclean: clean
	rm -rf vendor/pkg
