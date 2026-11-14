# meow — an operating system for cats
#
#   make            build build/meow.iso
#   make run        boot it in QEMU
#   make meowc      just the compiler / host player
#   make usb        build build/meow-usb.img (USB stick, old legacy-BIOS PCs)
#   make usb-run    boot that in QEMU as a 486 with 32 MB
#   make clean

BUILD   := build
GAMES   := meow chase pounce
MBC     := $(GAMES:%=$(BUILD)/%.mbc)

CC      := gcc
KCFLAGS := -m32 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
           -nostdlib -O2 -Wall -Wextra -I$(BUILD)

all: $(BUILD)/meow.iso

# ---- the compiler / host player -------------------------------------
$(BUILD)/meowc: compiler/meowc.c host/host.c vm/vm.c vm/vm.h | $(BUILD)
	$(CC) -O2 -Wall -Wextra -o $@ compiler/meowc.c host/host.c vm/vm.c

meowc: $(BUILD)/meowc

# ---- games: .meow -> .mbc bytecode ----------------------------------
$(BUILD)/%.mbc: games/%.meow $(BUILD)/meowc
	$(BUILD)/meowc build $< -o $@

$(BUILD)/games.h: $(MBC) tools/embed.py
	python3 tools/embed.py $@ $(MBC)

# ---- kernel ----------------------------------------------------------
$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	nasm -f elf32 $< -o $@

$(BUILD)/kernel.o: kernel/kernel.c vm/vm.h $(BUILD)/games.h
	$(CC) $(KCFLAGS) -c $< -o $@

$(BUILD)/kvm.o: vm/vm.c vm/vm.h | $(BUILD)
	$(CC) $(KCFLAGS) -c $< -o $@

$(BUILD)/kernel.bin: $(BUILD)/entry.o $(BUILD)/kernel.o $(BUILD)/kvm.o kernel/kernel.ld
	ld -m elf_i386 -T kernel/kernel.ld -o $(BUILD)/kernel.elf \
		$(BUILD)/entry.o $(BUILD)/kernel.o $(BUILD)/kvm.o
	objcopy -O binary $(BUILD)/kernel.elf $@

# ---- boot image + iso ------------------------------------------------
$(BUILD)/boot.bin: boot/boot.asm | $(BUILD)
	nasm -f bin $< -o $@

$(BUILD)/bootimg.bin: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	cat $(BUILD)/boot.bin $(BUILD)/kernel.bin > $@

$(BUILD)/meow.iso: $(BUILD)/bootimg.bin
	rm -rf $(BUILD)/iso
	mkdir -p $(BUILD)/iso/boot
	cp $(BUILD)/bootimg.bin $(BUILD)/iso/boot/meow.bin
	cp $(MBC) $(BUILD)/iso/
	xorriso -as mkisofs -quiet -V MEOW \
		-b boot/meow.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
		-o $@ $(BUILD)/iso
	@echo "purr: $@ is ready"

$(BUILD):
	mkdir -p $(BUILD)

run: $(BUILD)/meow.iso
	qemu-system-i386 -cdrom $(BUILD)/meow.iso

# ---- meow-usb: raw disk image for old legacy-BIOS PCs ----------------
# Same kernel sources, same link script, same games. Only usb/ differs:
# an MBR boot stage instead of El Torito, and a kernel wrapper compiled
# for a plain i486 with a keyboard poll that cannot hang.
USB         := $(BUILD)/usb
USB_SECTORS := 2048
USB_KCFLAGS := $(KCFLAGS) -march=i486 -mno-sse -mno-mmx -mno-sse2 \
               -Wno-unused-function

$(USB):
	mkdir -p $(USB)

$(USB)/kernel.o: usb/kernel_usb.c kernel/kernel.c vm/vm.h $(BUILD)/games.h | $(USB)
	$(CC) $(USB_KCFLAGS) -c $< -o $@

$(USB)/kvm.o: vm/vm.c vm/vm.h | $(USB)
	$(CC) $(USB_KCFLAGS) -c $< -o $@

$(USB)/kernel.bin: $(BUILD)/entry.o $(USB)/kernel.o $(USB)/kvm.o kernel/kernel.ld
	ld -m elf_i386 -T kernel/kernel.ld -o $(USB)/kernel.elf \
		$(BUILD)/entry.o $(USB)/kernel.o $(USB)/kvm.o
	objcopy -O binary $(USB)/kernel.elf $@

$(USB)/boot.bin: usb/boot.asm $(USB)/kernel.bin
	nasm -f bin -DIMAGE_SECTORS=$(USB_SECTORS) \
		-DKERNEL_SECTORS=$$(( ($$(wc -c < $(USB)/kernel.bin) + 511) / 512 )) \
		$< -o $@

$(BUILD)/meow-usb.img: $(USB)/boot.bin $(USB)/kernel.bin
	cat $(USB)/boot.bin $(USB)/kernel.bin > $@.tmp
	truncate -s $$(( $(USB_SECTORS) * 512 )) $@.tmp
	mv $@.tmp $@
	@echo "purr: $@ is ready"

usb: $(BUILD)/meow-usb.img

usb-run: $(BUILD)/meow-usb.img
	qemu-system-i386 -cpu 486 -m 32 -drive file=$(BUILD)/meow-usb.img,format=raw

clean:
	rm -rf $(BUILD)

.PHONY: all run clean meowc usb usb-run
