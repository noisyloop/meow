# how meow works

this document walks through everything that happens between "power on" and
"a cat catches a mouse", and then takes an honest look at every dependency
meow has and how far each one can be trusted.

## the big picture

```
 you write            meowc compiles         the vm runs it
 games/*.meow  ─────► build/*.mbc     ─────► inside the meow OS (from the ISO)
                      (bytecode)      └────► or in your terminal (meowc run)
```

meow is three small machines stacked on top of each other:

1. **a boot chain** that gets the CPU from 16-bit BIOS land into a 32-bit
   kernel,
2. **a kernel** that owns the screen, the keyboard, and the clock,
3. **a bytecode virtual machine** that runs compiled `.meow` programs.

the trick that keeps everything tiny is that the games never touch the
hardware. they only ever talk to the VM, and the VM asks the platform for
seven favors (print, clear, move cursor, set color, read key, random
number, sleep). implement those seven hooks and meow games run anywhere —
the kernel implements them with VGA and port I/O, the host player
implements them with ANSI escape codes.

## stage by stage

### 1. the BIOS loads 2 KB (`boot/boot.asm`)

the ISO is an El Torito "no emulation" boot CD. the BIOS reads the boot
catalog, finds our boot image, loads its first 2048 bytes at address
`0x7C00`, and jumps there in 16-bit real mode.

those 2048 bytes do exactly four things:

- read the **boot info table** that `xorriso` patched into the file at
  build time (it contains the LBA and length of the boot image on the CD),
- use BIOS `INT 13h` extended reads to pull the *rest* of the boot image
  (the kernel) off the CD to address `0x8400`, 32 KB at a time,
- enable the **A20 line** so memory above 1 MB doesn't wrap around,
- load a flat GDT, set the protected-mode bit in `CR0`, and far-jump into
  32-bit code, which jumps to the kernel at `0x8400`.

there is no GRUB, no second-stage loader, no filesystem driver — the boot
image is `boot.bin` and `kernel.bin` simply concatenated, so "loading the
kernel" is "keep reading the same file".

### 2. the kernel wakes up (`kernel/entry.asm`, `kernel/kernel.c`)

the first kernel bytes are a tiny assembly stub: set the stack to
`0x90000`, zero the `.bss` section (the loader only copies the file image,
so uninitialized C globals must be cleared by hand), and call `kmain()`.

the kernel is freestanding C — no libc, no interrupts, no paging, no
processes. it owns four pieces of hardware, all driven by polling:

| hardware       | how                                                        |
|----------------|------------------------------------------------------------|
| VGA text 80x25 | writes `char + attribute` words straight into `0xB8000`    |
| PS/2 keyboard  | polls port `0x64`, reads scancodes from `0x60`, translates set-1 scancodes to ASCII + arrow-key codes into a 16-slot queue |
| PIT timer      | channel 0 free-runs at 1.193 MHz; `nap(ms)` latches the countdown register and accumulates elapsed ticks (1193 ticks ≈ 1 ms) |
| serial COM1    | every character printed to the screen is mirrored to the serial port, so a headless VM still shows what happened |

`kmain()` seeds the random number generator from the PIT counter (key
press timings get stirred in later), draws the **"welcome to meow"**
banner, waits for a key, and enters the menu loop. picking a game calls
`vm_run()` on that game's bytecode with the kernel's seven hooks. when the
game halts, you're back in the menu.

### 3. the compiler (`compiler/meowc.c`)

`meowc` is a classic single-pass recursive-descent compiler, about 500
lines:

- **lexer**: integers, `"strings"` (with `\n`, `\"`, `\\`), `'c'` char
  literals, identifiers, operators, `~ comments`.
- **parser/emitter**: each grammar rule emits bytecode as it parses.
  there is no AST. forward jumps (`pounce`/`hiss`, `chase` exits,
  `scratch`) emit a placeholder offset that gets backpatched once the
  target address is known.
- **variables** are 32-bit integers in 64 numbered global slots; `purr`
  binds a name to the next free slot. a declared variable shadows the
  built-in key constants (`up`, `down`, `left`, `right`, `space`, `esc`,
  `enter`).
- **strings** are deduplicated into a string pool; code refers to them by
  index.

### 4. the bytecode format (`.mbc`)

everything little-endian:

```
"MBC1"                      magic, 4 bytes
u16  nstrs                  string pool
  { u16 len, bytes }  × nstrs
u32  codelen
  code bytes          × codelen
```

the VM (`vm/vm.c`) is a stack machine: a 256-slot value stack, 64 global
slots, and ~30 opcodes (push/load/store, arithmetic, comparisons,
`and/or/not`, two jumps, and one opcode per platform hook: print string,
print int, print char, clear, at, color, key, rand, nap, halt). division
by zero yields 0 — a cat does not crash over arithmetic. every fetch,
jump target, string index, and slot number is bounds-checked, so a corrupt
`.mbc` file returns an error code instead of scribbling over the kernel.

### 5. how games get onto the ISO

at build time, `meowc build` compiles each `games/*.meow` to `build/*.mbc`,
then `tools/embed.py` turns the bytecode into a C header (`build/games.h`)
containing plain byte arrays and a name table. the kernel includes that
header, so the games are *inside* `kernel.bin`. the `.mbc` files are also
copied onto the ISO as ordinary files for the day the kernel learns to
read ISO9660 (see the roadmap).

finally `xorriso` packs `boot.bin + kernel.bin` as the El Torito boot
image and stamps the boot info table into it. total: a ~9 KB operating
system on a ~384 KB ISO (almost all of which is ISO9660 filesystem
overhead — the format's minimum, not ours).

### 6. the same games on linux and windows (`host/host.c`)

`meowc run game.meow` compiles into memory and runs the identical VM with
terminal hooks instead of kernel hooks: ANSI escape sequences for
clear/at/color, raw non-blocking stdin (termios) for keys, `nanosleep`
for naps. on Windows (built with MinGW) the same file uses the console
API and `_kbhit`/`_getch`. this is why the bytecode is the portability
boundary: **one compiled game, three places it runs.**

## dependencies, and whether to trust them

meow's design goal is that *trust is concentrated at build time and
approaches zero at runtime*. here is the full list — there is nothing
else; no package managers, no vendored code, no network fetches during
build.

### what runs inside the OS at runtime

**nothing that isn't in this repository.** the shipped ISO contains only
code assembled/compiled from `boot/`, `kernel/`, `vm/`, and your `games/`.
there is no third-party runtime, no libc, no blobs. every byte the CPU
executes after the BIOS hands over control is auditable in this repo —
about 1,200 lines of C and assembly. this is the strongest trust statement
meow can make, and it is the reason the kernel links no external code.

the one thing beneath us: the **BIOS / VM firmware** (SeaBIOS in QEMU,
VirtualBox's BIOS, etc.). we must trust it completely — it loads us, and a
hypervisor can observe or alter anything a guest does. that is not a meow
decision; it is true of every operating system. trust level: **total and
unavoidable** (mitigation: run VMs you control, from vendors you trust).

### build-time dependencies

these touch the bits that end up in the ISO, so they matter most:

| tool | role | trust level |
|------|------|-------------|
| **gcc + binutils** (`ld`, `objcopy`) | compiles the kernel, VM, and `meowc` | **high, but load-bearing.** this is the classic "trusting trust" position: a malicious compiler could inject code into anything it builds, and no source audit would show it. practical footing: distro-packaged, cryptographically signed, and among the most scrutinized software on earth. paranoia options: build with `clang` as a cross-check and diff the binaries' behavior, or disassemble `kernel.bin` (it's ~7 KB — genuinely auditable). |
| **nasm** | assembles ~200 lines of boot/entry assembly | **high, and verifiable.** the output is so small you can check it byte-for-byte: `ndisasm -b16 build/boot.bin` and read along with `boot.asm`. |
| **xorriso** | packs the ISO, patches the boot info table | **high, and verifiable.** it arranges our bytes; it doesn't generate code. verify by mounting the ISO and checksumming the extracted boot image against `build/bootimg.bin` (the 56-byte boot info table region is the only expected difference). |
| **python3** | runs `tools/embed.py` (30 lines: bytes → C array) | **high, and trivially verifiable.** its entire output is `build/games.h`, a human-readable text file you can inspect. |
| **make** | orchestration only | **high.** it runs the commands in the Makefile; it contributes no bytes to the output. |

all of the above install from Debian/Ubuntu's signed repositories (apt
verifies package signatures against distro keys by default). the honest
summary: **you trust your Linux distribution's toolchain.** if that trust
fails, meow is the least of your problems — every program on the machine
came from the same pipeline.

### host-player-only dependencies

`meowc run` (playing games in a terminal, outside the OS) additionally
uses **glibc** and your **terminal emulator**. trust level: **the same
trust you already extend to every program on your host.** none of this
code exists inside the OS — the kernel build uses `-nostdlib
-ffreestanding` precisely so the ISO owes libc nothing.

### test-time dependencies

**QEMU** (or VirtualBox/VMware) is where the ISO gets booted. a VM sits
below the OS, so during a test run it is as powerful as the BIOS — but it
contributes zero bytes to any artifact. trust level: **needed only while
testing; irrelevant to what ships.**

### the "supply chain" summary, cat-sized

- number of language-package-manager dependencies (npm/pip/cargo): **0**
- number of vendored third-party source files: **0**
- number of binary blobs in the repo: **0**
- network access required to build (after installing distro tools): **none**
- third-party code in the shipped ISO: **none**
- what you ultimately trust: **your distro's signed toolchain, and the
  firmware of whatever machine runs the ISO** — the same two things every
  OS on earth trusts, except meow's audit surface on top of them is about
  1,900 lines, cat pictures included.
