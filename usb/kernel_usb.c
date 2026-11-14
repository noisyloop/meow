/* usb/kernel_usb.c — the meow-usb kernel
 *
 * This is the meow kernel, shared rather than copied: the whole of
 * kernel/kernel.c is included below, unmodified, and exactly two things
 * are swapped out for old hardware:
 *
 *   kbd_poll()  the keyboard poll. The original drains the PS/2
 *               controller until it reports empty, which never ends on a
 *               machine whose controller is absent or stuck. Ours is
 *               bounded, skips a missing controller, and drops mouse
 *               bytes.
 *   kmain()     ours flushes the controller first, then runs the
 *               original (renamed meow_main).
 *
 * The keyboard is only ever *read* (ports 0x60/0x64) — no controller
 * commands, no resets, no LED writes — because that is all a BIOS's
 * "Legacy USB Support" keyboard emulation reliably implements.
 *
 * Nothing here needs CPUID, PCI, ACPI, an APIC, or RAM above 1 MB.
 */

/* kernel.c writes "static void kbd_poll(void)" and calls "kbd_poll()".
 * Pasting the argument list onto the name sends the definition to
 * kbd_poll_void (the original, kept but unused) and every call to
 * kbd_poll_ (ours, below). */
#define kbd_poll(...) kbd_poll_##__VA_ARGS__()
#define kmain meow_main

static void kbd_poll_(void);

#include "../kernel/kernel.c"

#undef kbd_poll
#undef kmain

#define KBD_DATA    0x60
#define KBD_STATUS  0x64
#define KBD_OBF     0x01    /* output buffer full: a byte is waiting */
#define KBD_AUX     0x20    /* ...and it came from the mouse port */
#define KBD_ABSENT  0xFF    /* floating bus: no controller at all */

#define KBD_POLL_MAX   16   /* bytes handled per poll */
#define KBD_FLUSH_MAX 256   /* bytes discarded at start */

/* throw away whatever the BIOS (or an impatient cat) left in the buffer */
static void kbd_flush(void)
{
    int i;
    for (i = 0; i < KBD_FLUSH_MAX; i++) {
        mu8 st = inb(KBD_STATUS);
        if (st == KBD_ABSENT || !(st & KBD_OBF))
            break;
        (void)inb(KBD_DATA);
        (void)inb(0x80);     /* let a slow controller raise OBF again */
    }
}

static void kbd_poll_(void)
{
    static int e0;
    int n;
    for (n = 0; n < KBD_POLL_MAX; n++) {
        mu8 st = inb(KBD_STATUS);
        if (st == KBD_ABSENT || !(st & KBD_OBF))
            break;
        mu8 sc = inb(KBD_DATA);
        if (st & KBD_AUX)        /* mouse data is not a scancode */
            continue;
        rng_state ^= (rng_state << 7) ^ sc ^ pit_read();
        if (sc == 0xE0) {
            e0 = 1;
            continue;
        }
        if (sc & 0x80) {         /* key release */
            e0 = 0;
            continue;
        }
        char c = 0;
        if (e0) {
            e0 = 0;
            switch (sc) {
            case 0x48: c = (char)MEOW_KEY_UP; break;
            case 0x50: c = (char)MEOW_KEY_DOWN; break;
            case 0x4B: c = (char)MEOW_KEY_LEFT; break;
            case 0x4D: c = (char)MEOW_KEY_RIGHT; break;
            }
        } else {
            c = sc_ascii[sc];
        }
        if (c) {
            int next = (kq_head + 1) & 15;
            if (next != kq_tail) {
                key_queue[kq_head] = c;
                kq_head = next;
            }
        }
    }
}

void kmain(void)
{
    kbd_flush();
    meow_main();
}
