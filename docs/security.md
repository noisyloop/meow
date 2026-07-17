# meow security: inside cat, outside cat

meow runs in a virtual machine. the question everyone should ask about
anything running in a VM is: **can it get out?**

this document answers that for meow — honestly, in detail, and in a way
that's meant to teach you *how* to think about VM security, not just what
to believe about this one cat. it also records the findings of meow's
first security review.

## the inside cat and the outside cat

an **inside cat** lives in the house. it can do whatever it wants *inside*
— knock things off shelves, shred the couch, run at 3am. but it cannot
open the front door. the door only opens from the outside.

an **outside cat** is a different animal entirely. it goes where it wants.

meow is an inside cat. when you boot `meow.iso` in VirtualBox, everything
meow does — every instruction, every write to the screen, every keyboard
read — happens inside a house whose walls, doors, and windows are built
and enforced by the hypervisor and the CPU's virtualization hardware
(VT-x / AMD-V). meow doesn't hold the keys. it has never seen the keys.
the keys are on the outside.

concretely, meow lives in a stack of nested cages:

```
  .meow game            inside inside cat: bytecode in the meow VM,
     |                  no pointers, no memory access, no port I/O
  meow kernel           inside cat: ring 0, but ring 0 *of the guest*
     |
  VirtualBox / QEMU     the house: emulates the "hardware" meow touches
     |
  CPU virtualization    the walls: VT-x traps privileged guest actions
     |
  your host OS          the outside world
```

"ring 0" sounds powerful, and inside the guest it is — meow's kernel has
no MMU protections, no privilege levels, no processes; a game that broke
out of the bytecode VM would own the whole meow kernel. but *guest* ring 0
is still inside the house. from the hypervisor's point of view, a guest
kernel throwing privileged instructions around is just a cat being loud in
the living room. every port write, every weird CPU state, every DMA
request gets trapped or emulated by the hypervisor before it means
anything real.

## so *could* meow escape?

**not by design, and not plausibly by accident. here is the honest
breakdown.**

a VM escape is never "the guest decided to leave." it is always **a bug in
the hypervisor** — the guest feeds malformed input to a piece of emulated
hardware, the emulation code (which runs *outside* the house) mishandles
it, and the guest gains code execution on the host. famous real examples,
worth reading for anyone learning this field:

- **VENOM (CVE-2015-3456)** — QEMU's virtual *floppy controller* had a
  buffer overflow reachable from any guest, even ones with no floppy
  configured. lesson: attack surface is what the hypervisor *emulates*,
  not what the guest *uses*.
- **CVE-2018-2698** — VirtualBox's e1000 *network card* emulation could be
  overflowed from the guest. lesson: network devices are big, complex, and
  historically the most productive escape surface.
- assorted VirtualBox **3D acceleration** bugs — the guest could reach the
  host's graphics stack through the shared-graphics service. lesson:
  "convenience integrations" (3D, shared folders, clipboard, guest
  additions) are doors deliberately built into the wall, and doors have
  locks that break.

so an escape needs three things:

1. **code execution inside the guest.** for meow that means either a bug
   in the ~200-line bytecode interpreter (see the review below) or code
   that was compiled into the ISO — which you built yourself, from ~1,900
   auditable lines.
2. **a reachable, unpatched hypervisor bug.** meow's device surface is
   the smallest it could realistically be, and it is the *oldest, most
   audited* emulation code that exists. the complete list of "hardware"
   meow ever touches:
   - VGA text memory (`0xB8000`) — writes characters
   - PS/2 keyboard controller (ports `0x60`, `0x64`) — reads scancodes
   - PIT timer (ports `0x40`, `0x43`) — reads a countdown register
   - VGA cursor registers (ports `0x3D4`, `0x3D5`)
   - serial COM1 (`0x3F8`–`0x3FF`) — writes debug text
   - A20 gate (port `0x92`) and BIOS interrupts `10h`/`13h`/`15h`, used
     only during the 2 KB boot stage
   no network card, no USB, no audio, no disk writes, no floppy, no 3D,
   no guest additions, no shared anything. meow doesn't just avoid using
   these — it *contains no driver code for them at all*, so even a fully
   compromised meow kernel would have to bring its own exploit with it.
3. **an exploit.** meow ships no code that manipulates emulated devices
   outside their documented, boring behavior, and its only input formats
   (keyboard scancodes and its own bytecode) are bounds-checked.

put together: meow has no means, no built-in vector, and — being an
operating system for cats — no motive. the residual risk is the same
residual risk *every* guest OS carries: an unpatched hypervisor bug in a
device that's still attached to the VM. that risk is managed from the
outside (see the checklist below), which is exactly where the inside/
outside cat model says it must be managed.

### "if it could" — what it would look like

for learners, the hypothetical escape path from a meow game to the host
would have to be: craft a malicious `.mbc` → find a hole in the bytecode
interpreter's bounds checks → own the meow kernel (guest ring 0) → use
that to poke a vulnerable emulated device with crafted input → exploit an
unpatched VirtualBox bug → run code on the host. five independent locks,
each on a different door, most of them not meow's door. defense means
making *your* locks good (steps 1–2) and keeping the landlord's locks
patched (steps 3–5: update VirtualBox, detach devices).

## the security review (2026-07)

a review of meow's actual code, with the trust boundaries in mind. the
interesting boundary is the one place untrusted bytes meet C code: the
bytecode VM.

**findings, fixed during the review:**

1. **integer-cast bypass in `.mbc` validation** (`vm/vm.c`). `codelen` is
   read from the file as an unsigned 32-bit value but was validated with
   `pos + (int)codelen > len`. a crafted file with `codelen = 0xFFFFFFF0`
   casts to a negative int, passed the check, and the interpreter would
   then read far out of bounds — a classic signed/unsigned confusion.
   fixed by comparing in unsigned space (`codelen > (mu32)(len - pos)`),
   same for string lengths. regression-tested with crafted malicious
   files; both are now rejected.
2. **missing cursor clamp in the host player** (`host/host.c`). the
   kernel clamped `at x, y` to the 80x25 screen; the terminal player
   passed values straight into an ANSI escape sequence. harmless in
   practice, but the two platforms should enforce identical bounds —
   fixed to clamp like the kernel.

**verified properties (things that held up):**

- every VM opcode bounds-checks: stack pushes, slot numbers, jump targets,
  string indices, operand fetches. a corrupt or hostile `.mbc` returns an
  error code instead of corrupting memory.
- the bytecode instruction set has **no memory, pointer, or port
  operations** — a game physically cannot express "write to address X."
  the seven platform hooks (print, clear, at, color, key, rand, nap) are
  the entire world a game can touch.
- division/modulo by zero yield 0 rather than faulting.
- keyboard input is a fixed-size queue of translated scancodes; no
  guest-controllable lengths anywhere in the input path.
- the kernel writes only to VGA text memory and its own statics; there is
  no dynamic allocation anywhere in the OS.

**known non-protections, by design (be aware):**

- **inside the guest there are no walls at all.** no MMU, no ring 3, no
  process isolation. the bytecode VM is meow's only internal security
  boundary. that is an honest tradeoff for a 9 KB OS — the *outer* wall
  (the hypervisor) is the load-bearing one.
- a game can busy-loop or `nap` forever; the menu only returns when the
  game halts. denial of service against your own cat is possible. reset
  the VM.
- the boot stage trusts the boot info table and the ISO it came from. the
  ISO is the trusted computing base: whoever builds it owns the system.
  build your own from source.
- games baked into the kernel at build time are trusted by definition.
  the day meow loads `.mbc` files from the disc at runtime (roadmap),
  those files become untrusted input — the VM's checks were written, and
  now fuzzed-by-hand, with that future in mind.

## checklist: running meow with maximum inside-cat-ness

all enforcement lives outside the guest, so this is a *host-side* list:

- keep VirtualBox/QEMU **updated** — escapes die by patch.
- give the VM **no network adapter**. meow has no network stack; an
  attached NIC is pure hypervisor attack surface with zero benefit.
- disable **audio, USB, 3D acceleration, shared folders, shared
  clipboard, drag-and-drop**. meow uses none of them (VirtualBox "Other/
  Unknown" profile with EFI off, which is how meow boots anyway, already
  defaults most of this to off).
- don't install guest additions (you couldn't anyway — meow wouldn't know
  what to do with them, which is the point).
- only boot ISOs you built yourself, and only run `.mbc` files you
  compiled yourself with `meowc`.
- snapshots are cheap. take one, let the cat do whatever, roll back.

## for people who want to learn more

the concepts to search for, roughly in order: privilege rings and why
guest ring 0 ≠ host ring 0 → hardware virtualization (Intel VT-x / AMD-V,
VM exits) → device emulation attack surface → DMA and why the IOMMU
matters → the VENOM and CVE-2018-2698 writeups → VM hardening guides.
meow is a good specimen to study precisely because it is small enough to
hold in your head: you can enumerate its *entire* interaction surface
with the hypervisor on one hand, then reason about each finger.

an inside cat that knows exactly where its walls are is a safe and happy
cat. meow knows.
