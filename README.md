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

## roadmap

- native code generation (`meowc build --exe`) for standalone `.exe` /
  ELF programs
- reading `.mbc` files from the ISO9660 filesystem at runtime (they're
  already on the disc) instead of baking them into the kernel
- a meow shell, sprites, sound (purring through the PC speaker)
- more cats

## license

see LICENSE.
