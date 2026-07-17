/* kernel/kernel.c — the meow operating system
 *
 * an operating system for cats. it boots, it says meow, it plays cat games.
 *
 * Everything here is freestanding: VGA text at 0xB8000, polled PS/2
 * keyboard, PIT channel 0 for time, and the shared meow VM for games.
 * Games are .meow programs compiled to bytecode and baked into the image
 * at build time (see build/games.h).
 */
#include "../vm/vm.h"
#include "games.h"

/* ---------------- port i/o ---------------- */

static inline void outb(mu16 port, mu8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline mu8 inb(mu16 port)
{
    mu8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* ---------------- serial (COM1) — debug mirror of boot messages ------- */

#define COM1 0x3F8

static void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);   /* 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c)
{
    int i;
    for (i = 0; i < 10000 && !(inb(COM1 + 5) & 0x20); i++)
        ;
    outb(COM1, (mu8)c);
}

static void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s++);
    }
}

/* ---------------- VGA text mode, 80x25 ---------------- */

#define VGA ((volatile mu16 *)0xB8000)
#define COLS 80
#define ROWS 25

static int cur_x, cur_y;
static mu8 attr = 0x0D;      /* light magenta on black — very meow */

static void move_hw_cursor(void)
{
    mu16 pos = (mu16)(cur_y * COLS + cur_x);
    outb(0x3D4, 14); outb(0x3D5, pos >> 8);
    outb(0x3D4, 15); outb(0x3D5, pos & 255);
}

static void vga_clear(void)
{
    int i;
    for (i = 0; i < COLS * ROWS; i++)
        VGA[i] = (mu16)(attr << 8 | ' ');
    cur_x = cur_y = 0;
    move_hw_cursor();
}

static void vga_scroll(void)
{
    int i;
    for (i = 0; i < COLS * (ROWS - 1); i++)
        VGA[i] = VGA[i + COLS];
    for (i = COLS * (ROWS - 1); i < COLS * ROWS; i++)
        VGA[i] = (mu16)(attr << 8 | ' ');
    cur_y = ROWS - 1;
}

static void vga_putc(char c)
{
    if (c == '\n') {
        cur_x = 0;
        cur_y++;
    } else {
        VGA[cur_y * COLS + cur_x] = (mu16)(attr << 8 | (mu8)c);
        cur_x++;
        if (cur_x >= COLS) {
            cur_x = 0;
            cur_y++;
        }
    }
    if (cur_y >= ROWS)
        vga_scroll();
}

static void vga_puts_n(const char *s, int n)
{
    while (n-- > 0) {
        serial_putc(*s == '\n' ? '\r' : *s);
        if (*s == '\n')
            serial_putc('\n');
        vga_putc(*s++);
    }
    move_hw_cursor();
}

static void vga_puts(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    vga_puts_n(s, n);
}

static void vga_at(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= COLS) x = COLS - 1;
    if (y < 0) y = 0;
    if (y >= ROWS) y = ROWS - 1;
    cur_x = x;
    cur_y = y;
    move_hw_cursor();
}

static void vga_color(int c)
{
    attr = (mu8)(c & 15);
}

/* ---------------- PIT channel 0 — milliseconds without interrupts ----- */

#define PIT_HZ 1193182

static mu16 pit_last;

static mu16 pit_read(void)
{
    mu8 lo, hi;
    outb(0x43, 0x00);            /* latch channel 0 */
    lo = inb(0x40);
    hi = inb(0x40);
    return (mu16)(hi << 8 | lo);
}

static void pit_init(void)
{
    outb(0x43, 0x34);            /* channel 0, rate generator */
    outb(0x40, 0);               /* divisor 65536 */
    outb(0x40, 0);
    pit_last = pit_read();
}

/* ticks elapsed since the previous call (the counter counts down) */
static mu32 pit_delta(void)
{
    mu16 now = pit_read();
    mu16 d = (mu16)(pit_last - now);
    pit_last = now;
    return d;
}

/* ---------------- rng ---------------- */

static mu32 rng_state = 0xCA7CA7u; /* re-seeded in kmain */

static mu32 rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int kern_rnd(int n)
{
    return (int)(rng_next() % (mu32)n);
}

/* ---------------- keyboard (polled PS/2, scancode set 1) -------------- */

static char key_queue[16];
static int kq_head, kq_tail;

static const char sc_ascii[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8,
    9, 'q','w','e','r','t','y','u','i','o','p','[',']', 10,
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ',
};

static void kbd_poll(void)
{
    static int e0;
    while (inb(0x64) & 1) {
        mu8 sc = inb(0x60);
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
        } else if (sc < 128) {
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

static int kern_key(void)
{
    kbd_poll();
    if (kq_tail == kq_head)
        return 0;
    char c = key_queue[kq_tail];
    kq_tail = (kq_tail + 1) & 15;
    return (mu8)c;
}

static void kern_nap(int ms)
{
    /* 1193.182 ticks per millisecond; close enough for a cat */
    mu32 target = (mu32)ms * 1193;
    mu32 elapsed = 0;
    pit_delta();
    while (elapsed < target) {
        kbd_poll();
        elapsed += pit_delta();
    }
}

static int wait_key(void)
{
    int k;
    while (!(k = kern_key()))
        kern_nap(5);
    return k;
}

/* ---------------- meow vm hooks ---------------- */

static const vm_hooks kernel_hooks = {
    vga_puts_n, vga_clear, vga_at, vga_color,
    kern_key, kern_rnd, kern_nap,
};

/* ---------------- welcome + menu ---------------- */

static void center(int y, const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    vga_at((COLS - n) / 2, y);
    vga_puts(s);
}

static void banner(void)
{
    vga_color(13);
    vga_clear();
    vga_color(11);
    center(7,  "|\\---/|");
    center(8,  "| o_o |");
    center(9,  " \\_^_/ ");
    vga_color(13);
    center(12, "welcome to meow");
    vga_color(5);
    center(14, "the operating system for cats");
    vga_color(8);
    center(18, "press any key to purr");
    serial_puts("\nwelcome to meow\n");
}

static void menu(void)
{
    int i;
    vga_color(13);
    vga_clear();
    vga_color(11);
    center(3, "=^..^=   meow games   =^..^=");
    for (i = 0; i < MEOW_NGAMES; i++) {
        vga_color(15);
        vga_at(30, 6 + i * 2);
        vga_putc((char)('1' + i));
        vga_puts("  ");
        vga_color(10);
        vga_puts(meow_games[i].name);
    }
    vga_color(8);
    center(6 + MEOW_NGAMES * 2 + 2, "press a number to play");
}

void kmain(void)
{
    serial_init();
    pit_init();
    rng_state = (mu32)(pit_read() * 2654435761u) | 1;

    banner();
    wait_key();

    for (;;) {
        menu();
        int k = wait_key();
        if (k >= '1' && k < '1' + MEOW_NGAMES) {
            const struct meow_game *g = &meow_games[k - '1'];
            serial_puts("running: ");
            serial_puts(g->name);
            serial_puts("\n");
            vm_run(g->data, g->len, &kernel_hooks);
        }
    }
}
