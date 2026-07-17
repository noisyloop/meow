/* vm/vm.c — the meow bytecode interpreter (freestanding, no libc) */
#include "vm.h"

#define VM_STACK   256
#define VM_GLOBALS 64
#define VM_STRINGS 128

static mu16 rd16(const mu8 *p) { return (mu16)(p[0] | (p[1] << 8)); }
static mu32 rd32(const mu8 *p)
{
    return (mu32)p[0] | ((mu32)p[1] << 8) | ((mu32)p[2] << 16) | ((mu32)p[3] << 24);
}

static void print_int(mi32 v, const vm_hooks *h)
{
    char buf[12];
    int i = 12;
    mu32 u = (mu32)v;
    if (v < 0)
        u = (mu32)(-v);
    do {
        buf[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u && i > 1);
    if (v < 0)
        buf[--i] = '-';
    h->puts_n(buf + i, 12 - i);
}

int vm_run(const mu8 *image, int len, const vm_hooks *h)
{
    const mu8 *strs[VM_STRINGS];
    mu16 slen[VM_STRINGS];
    mi32 stack[VM_STACK];
    mi32 globals[VM_GLOBALS];
    const mu8 *code;
    mu32 codelen, pc = 0;
    int sp = 0, nstrs, i, pos;

    if (len < 10 || image[0] != 'M' || image[1] != 'B' || image[2] != 'C' || image[3] != '1')
        return -1;
    nstrs = rd16(image + 4);
    if (nstrs > VM_STRINGS)
        return -1;
    pos = 6;
    for (i = 0; i < nstrs; i++) {
        if (pos + 2 > len)
            return -1;
        slen[i] = rd16(image + pos);
        pos += 2;
        if (pos + slen[i] > len)
            return -1;
        strs[i] = image + pos;
        pos += slen[i];
    }
    if (pos + 4 > len)
        return -1;
    codelen = rd32(image + pos);
    pos += 4;
    if (pos + (int)codelen > len)
        return -1;
    code = image + pos;

    for (i = 0; i < VM_GLOBALS; i++)
        globals[i] = 0;

#define POP()    (sp > 0 ? stack[--sp] : 0)
#define PUSH(v)  do { mi32 t_ = (v); if (sp < VM_STACK) stack[sp++] = t_; else return -2; } while (0)
#define BINOP(expr) do { mi32 b = POP(), a = POP(); PUSH(expr); } while (0)

    while (pc < codelen) {
        mu8 op = code[pc++];
        switch (op) {
        case OP_HALT:
            return 0;
        case OP_PUSH:
            if (pc + 4 > codelen) return -3;
            PUSH((mi32)rd32(code + pc));
            pc += 4;
            break;
        case OP_LOAD:
            if (pc >= codelen || code[pc] >= VM_GLOBALS) return -3;
            PUSH(globals[code[pc++]]);
            break;
        case OP_STORE:
            if (pc >= codelen || code[pc] >= VM_GLOBALS) return -3;
            globals[code[pc++]] = POP();
            break;
        case OP_ADD: BINOP(a + b); break;
        case OP_SUB: BINOP(a - b); break;
        case OP_MUL: BINOP(a * b); break;
        case OP_DIV: BINOP(b ? a / b : 0); break;
        case OP_MOD: BINOP(b ? a % b : 0); break;
        case OP_NEG: PUSH(-POP()); break;
        case OP_EQ:  BINOP(a == b); break;
        case OP_NE:  BINOP(a != b); break;
        case OP_LT:  BINOP(a < b); break;
        case OP_GT:  BINOP(a > b); break;
        case OP_LE:  BINOP(a <= b); break;
        case OP_GE:  BINOP(a >= b); break;
        case OP_AND: BINOP(a && b); break;
        case OP_OR:  BINOP(a || b); break;
        case OP_NOT: PUSH(!POP()); break;
        case OP_JMP:
            if (pc + 4 > codelen) return -3;
            pc = rd32(code + pc);
            if (pc > codelen) return -3;
            break;
        case OP_JZ: {
            mu32 target;
            if (pc + 4 > codelen) return -3;
            target = rd32(code + pc);
            pc += 4;
            if (!POP()) {
                if (target > codelen) return -3;
                pc = target;
            }
            break;
        }
        case OP_PRINTS: {
            mu16 idx;
            if (pc + 2 > codelen) return -3;
            idx = rd16(code + pc);
            pc += 2;
            if (idx >= nstrs) return -3;
            h->puts_n((const char *)strs[idx], slen[idx]);
            break;
        }
        case OP_PRINTI:
            print_int(POP(), h);
            break;
        case OP_PRINTC: {
            char c = (char)POP();
            h->puts_n(&c, 1);
            break;
        }
        case OP_CLEAR:
            h->clear();
            break;
        case OP_AT: {
            mi32 y = POP(), x = POP();
            h->at(x, y);
            break;
        }
        case OP_COLOR:
            h->color(POP() & 15);
            break;
        case OP_KEY:
            PUSH(h->key());
            break;
        case OP_RAND: {
            mi32 n = POP();
            PUSH(n > 0 ? h->rnd(n) : 0);
            break;
        }
        case OP_NAP: {
            mi32 ms = POP();
            if (ms > 0)
                h->nap(ms);
            break;
        }
        default:
            return -4;
        }
    }
    return 0;
}
