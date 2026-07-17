/* vm/vm.h — the meow bytecode virtual machine
 *
 * One tiny VM, two homes: it runs compiled .meow games inside the meow
 * kernel (bare metal, no libc) and inside meowc on a Linux/Windows host.
 * The platform supplies the hooks; the VM supplies everything else.
 */
#ifndef MEOW_VM_H
#define MEOW_VM_H

/* freestanding-safe integer types */
typedef unsigned char  mu8;
typedef unsigned short mu16;
typedef unsigned int   mu32;
typedef int            mi32;

/* bytecode image: "MBC1", u16 nstrs, {u16 len, bytes}*, u32 codelen, code */
#define MBC_MAGIC "MBC1"

enum {
    OP_HALT = 0x00,
    OP_PUSH = 0x01,   /* i32le operand */
    OP_LOAD = 0x02,   /* u8 slot */
    OP_STORE = 0x03,  /* u8 slot */
    OP_ADD = 0x04, OP_SUB = 0x05, OP_MUL = 0x06, OP_DIV = 0x07,
    OP_MOD = 0x08, OP_NEG = 0x09,
    OP_EQ = 0x0A, OP_NE = 0x0B, OP_LT = 0x0C, OP_GT = 0x0D,
    OP_LE = 0x0E, OP_GE = 0x0F,
    OP_JMP = 0x10,    /* u32 code offset */
    OP_JZ = 0x11,     /* u32 code offset */
    OP_PRINTS = 0x12, /* u16 string index */
    OP_PRINTI = 0x13,
    OP_PRINTC = 0x14,
    OP_CLEAR = 0x15,
    OP_AT = 0x16,     /* pops y, x */
    OP_COLOR = 0x17,
    OP_KEY = 0x18,
    OP_RAND = 0x19,   /* pops n, pushes 0..n-1 */
    OP_NAP = 0x1A,    /* pops milliseconds */
    OP_AND = 0x1B, OP_OR = 0x1C, OP_NOT = 0x1D,
};

/* key codes shared by every meow platform */
#define MEOW_KEY_ENTER 10
#define MEOW_KEY_ESC   27
#define MEOW_KEY_UP    128
#define MEOW_KEY_DOWN  129
#define MEOW_KEY_LEFT  130
#define MEOW_KEY_RIGHT 131

typedef struct {
    void (*puts_n)(const char *s, int n); /* write n chars at the cursor */
    void (*clear)(void);                  /* clear screen, cursor home */
    void (*at)(int x, int y);             /* move cursor (0-based, 80x25) */
    void (*color)(int c);                 /* VGA color 0..15 */
    int  (*key)(void);                    /* next key or 0, non-blocking */
    int  (*rnd)(int n);                   /* uniform-ish 0..n-1 */
    void (*nap)(int ms);                  /* sleep */
} vm_hooks;

/* returns 0 on clean HALT, negative on a bad image or runtime fault */
int vm_run(const mu8 *image, int len, const vm_hooks *hooks);

#endif
