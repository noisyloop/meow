# meow — an operating system for cats
#
#   make            build build/meow.iso
#   make run        boot it in QEMU
#   make meowc      just the compiler / host player
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

clean:
	rm -rf $(BUILD)

.PHONY: all run clean meowc
