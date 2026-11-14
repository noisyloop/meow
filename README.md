# meow

an operating system for cats. warning, the .iso should be treated with absolute zero trust unless you are a computer expert, same goes for the VM. Read /docs for more.

meow is a tiny operating system with its own programming language (also
called meow), its own compiler (`meowc`), and its own games — all of them
about cats, obviously. it is inspired by the spirit of TempleOS, but it is
far, far smaller: the whole OS fits in about 9 KB.

when you boot the ISO in a VM you'll see:

```
        |\---/|
        | o_o |
         \_^_/

    welcome to meow
the operating system for cats

   press any key to purr
```

press a key and you get the game menu. the first game ever written for
meow is `meow.meow`: a window opens and it says "meow". that's it. that's
the game.

## quick start

you need: `gcc` (with 32-bit support), `nasm`, `xorriso`, `python3`, and
`qemu` to try it.

```sh
sudo apt install nasm xorriso qemu-system-x86 gcc-multilib   # debian/ubuntu

make        # builds build/meow.iso
make run    # boots it in QEMU
```

the ISO boots in most modern VMs — QEMU, VirtualBox, VMware, Hyper-V —
as long as the VM uses **BIOS (legacy) boot**, not UEFI. attach
`build/meow.iso` as a CD/DVD drive and start the VM.

## the meow language

`.meow` files are compiled by `meowc` to meow bytecode (`.mbc`), which runs
on the meow virtual machine. the *same* bytecode runs inside the meow OS
and on Linux/Windows hosts, so you can develop games in a terminal and ship
them on the ISO unchanged.

```
~ comments start with a tilde and run to the end of the line

purr lives = 9              ~ declare a variable (integers only)
lives = lives - 1           ~ assign
meow "hello ", lives        ~ print items, then a newline
mew "no newline"            ~ print items, no newline

pounce lives < 1 {          ~ if
  meow "out of lives!"
} hiss {                    ~ else
  meow "still going"
}

chase lives > 0 {           ~ while
  lives = lives - 1
  pounce lives == 3 { scratch }   ~ scratch = break
}

nap 100                     ~ sleep 100 milliseconds
clear                       ~ clear the screen (80x25 text)
at 10, 5                    ~ move the cursor to column 10, row 5
color 13                    ~ set text color (VGA colors 0..15)
```

expressions: `+ - * / %`, comparisons `== != < > <= >=`, `and or not`,
parentheses, character literals like `'a'`, and two built-ins:

- `key` — the next key pressed, or 0 (non-blocking)
- `random(n)` — a random number from 0 to n-1

key constants: `up down left right space esc enter` (a declared variable
with the same name shadows the constant).

### using the compiler

```sh
make meowc                          # build the compiler
build/meowc run games/chase.meow    # compile & play right in your terminal
build/meowc build games/chase.meow -o chase.mbc    # compile to bytecode
build/meowc runbc chase.mbc         # play compiled bytecode
```

`meowc` also builds on Windows with MinGW (`x86_64-w64-mingw32-gcc
compiler/meowc.c host/host.c vm/vm.c -o meowc.exe`), which gives you
`meowc.exe` — meow games as programs on Windows today; true native `.exe`
code generation is on the roadmap.

## the games

| game          | what a cat does                                            |
|---------------|------------------------------------------------------------|
| `meow.meow`   | a window opens and says "meow". the original meow program. |
| `chase.meow`  | move the cat with the arrow keys, catch the mouse          |
| `pounce.meow` | press space when the mouse runs under your paw             |

new games: drop a `.meow` file in `games/`, add its name to `GAMES` in the
Makefile, `make run`. it appears in the boot menu.

## how it works

the full walkthrough — boot chain, kernel, compiler, bytecode format, and
an honest audit of every dependency and how far it can be trusted — lives
in [docs/how-it-works.md](docs/how-it-works.md). the short version:

```
boot/boot.asm     2 KB El Torito boot stage: loads the kernel from the CD,
                  enables A20, enters 32-bit protected mode  (no GRUB)
kernel/           freestanding C kernel at 0x8400: VGA text, polled PS/2
                  keyboard, PIT timer, welcome screen, game menu
vm/vm.c           the meow bytecode VM — shared, byte-for-byte, between
                  the kernel and the host player
compiler/meowc.c  the meow language compiler (single-pass, ~500 lines)
host/host.c       terminal front-end (ANSI + termios / Windows console)
games/*.meow      the games, compiled at build time and baked into the ISO
```

memory map: boot stage at `0x7C00`, kernel at `0x8400`, stack at
`0x90000`, VGA text buffer at `0xB8000`. no interrupts, no paging, no
processes — a cat does one thing at a time, intensely.

## meow-usb

a second build of meow for older legacy-BIOS PCs (roughly early-2000s
hardware and before) that boot from a USB stick. same kernel, same VM,
same games — only the code in `usb/` is different. no CD support here;
the ISO build above is untouched.

```sh
make usb        # builds build/meow-usb.img (1 MiB raw disk image)
make usb-run    # boots it in QEMU as a 486 with 32 MB of RAM
```

### flash, plug in, boot

1. `make usb`
2. open balenaEtcher, pick `build/meow-usb.img`, pick your USB stick,
   flash. (or: `sudo dd if=build/meow-usb.img of=/dev/sdX bs=1M conv=fsync`
   — check `/dev/sdX` twice, it erases the stick.) Etcher may say the
   image "doesn't look bootable" for an operating system it doesn't
   know; continue anyway.
3. plug the stick into the old PC, power on, open the BIOS boot menu and
   choose the USB device. if the BIOS offers USB-HDD, USB-ZIP and
   USB-FDD modes, any of them should work; try USB-HDD first.
4. if the keyboard is a USB one, turn on "Legacy USB Support" (sometimes
   "USB Keyboard Support") in BIOS setup.

a USB mass-storage emulator that serves `meow-usb.img` works the same way.

### image layout

```
byte offset   sector   loaded at   what
-----------   ------   ---------   ------------------------------------------
0x000000      0        0x7C00      jmp + nop
0x000003                           FAT-style BPB (for floppy/ZIP emulation)
0x00003E                           boot code: read sectors 1..N via INT 13h
0x0001B8                           disk signature "MEOW"
0x0001BE                           partition table: 1 active entry, type 0xDA,
                                   LBA 1 .. end of image
0x0001FE                           55 AA
0x000200      1-3      0x7E00      stage 2: A20, GDT, protected mode
0x000800      4..      0x8400      kernel (flat binary, about 7 KB)
  ...                              zero padding
0x100000      2048                 end of image (1 MiB)
```

the first 2048 bytes mirror the CD boot stage (which is also 2048 bytes
at `0x7C00`), so the kernel links at `0x8400` in both builds with the
same `kernel/kernel.ld`.

### what you see while it boots

one character per stage, printed with the BIOS, then the welcome screen:

| char | meaning                                              |
|------|------------------------------------------------------|
| `M`  | boot sector is running                               |
| `L`  | reading with INT 13h extensions (LBA)                |
| `C`  | reading with CHS, geometry from the BIOS             |
| `K`  | stage 2 and kernel are loaded                        |
| `A`  | A20 line is on                                       |
| `P`  | entering protected mode                              |

a healthy boot flashes `MLKAP` or `MCKAP`. on failure it prints `!` and a
letter, and stops:

| error | meaning                                                         |
|-------|-----------------------------------------------------------------|
| `!G`  | no LBA extensions and the BIOS reported no disk geometry        |
| `!D`  | a sector could not be read (3 tries with disk resets, LBA then CHS) |
| `!S`  | stage 2 signature missing — image truncated or badly flashed    |
| `!A`  | the A20 line could not be enabled                               |

### what is different from the ISO build

- **boot** (`usb/boot.asm`): MBR instead of El Torito. keeps the BIOS
  drive number from `DL`, checks INT 13h `AH=41h` and reads with
  `AH=42h`, otherwise (or if that fails) CHS `AH=02h` with geometry from
  `AH=08h`. one sector per read, so nothing crosses a 64 KB boundary;
  three tries per sector with a disk reset between.
- **A20**: test first, then INT 15h `AX=2401h`, then the keyboard
  controller, then port `0x92` — re-testing after each step.
- **cpu**: the kernel is compiled `-march=i486 -mno-sse -mno-mmx
  -mno-sse2` and uses no CPUID. (the ISO build uses the compiler's
  default, which emits `cmov` and so needs a newer CPU.)
- **keyboard** (`usb/kernel_usb.c`): the controller is flushed at start
  and every poll is bounded, so a missing or stuck controller can't hang
  the cat. it only reads ports `0x60`/`0x64` and never sends controller
  commands, which is what BIOS USB keyboard emulation supports.
- no assumptions about RAM above 1 MB, ACPI, APIC or PCI; VGA text
  80x25 at `0xB8000` only.

`usb/kernel_usb.c` does not copy the kernel: it `#include`s
`kernel/kernel.c` as-is and swaps in its own keyboard poll and a `kmain`
that flushes the keyboard first.

### testing in QEMU

all three as a 486 with 32 MB. press a key at the welcome screen, then
`1` to check the keyboard.

```sh
# as a raw hard disk (this is what `make usb-run` does)
qemu-system-i386 -cpu 486 -m 32 -drive file=build/meow-usb.img,format=raw

# as a USB stick
qemu-system-i386 -cpu 486 -m 32 -usb \
    -drive if=none,id=stick,file=build/meow-usb.img,format=raw \
    -device usb-storage,drive=stick,bootindex=0

# as a floppy — exercises the BPB / CHS path (prints MCKAP)
qemu-system-i386 -cpu 486 -m 32 -fda build/meow-usb.img
```

without a display, add `-display none -serial stdio`: the kernel mirrors
its text to COM1, so `welcome to meow` on the terminal means it booted.

the original build is checked the same way it always was: `make` and
`make run`. adding meow-usb changes no file the ISO is built from.

## security

meow is an inside cat: it runs in a VM and cannot open the door from the
inside. what that means precisely — the sandbox layers, whether escape is
possible, the full list of "hardware" meow touches, and the findings of
its first security review — is in [docs/security.md](docs/security.md).

## roadmap

- native code generation (`meowc build --exe`) for standalone `.exe` /
  ELF programs
- reading `.mbc` files from the ISO9660 filesystem at runtime (they're
  already on the disc) instead of baking them into the kernel
- a meow shell, sprites, sound (purring through the PC speaker)
- more cats

## license

see LICENSE.
