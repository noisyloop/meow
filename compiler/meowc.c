/* compiler/meowc.c — the meow language compiler
 *
 * meowc turns .meow source into meow bytecode (.mbc) that runs inside the
 * meow operating system or right here on the host.
 *
 *   meowc build game.meow -o game.mbc     compile
 *   meowc run game.meow                   compile and play in this terminal
 *   meowc runbc game.mbc                  play precompiled bytecode
 *
 * The language, briefly:
 *
 *   ~ comments run to end of line
 *   purr lives = 9                 declare a variable
 *   lives = lives - 1              assign
 *   meow "hello ", lives           print items, then newline
 *   mew "no newline"               print items, no newline
 *   pounce lives < 1 { ... }       if
 *   hiss { ... }                   else (right after a pounce block)
 *   chase 1 { ... }                while
 *   scratch                        break out of the current chase
 *   nap 100                        sleep milliseconds
 *   clear / at x, y / color c      screen control (80x25, VGA colors 0..15)
 *   key                            expression: next key or 0 (non-blocking)
 *   random(n)                      expression: 0..n-1
 *   'a', up, down, left, right, space, esc, enter    key constants
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../vm/vm.h"

int host_run(const unsigned char *image, int len); /* host/host.c */

/* ---------------- error handling ---------------- */

static const char *src_name;
static int line = 1;

static void die(const char *msg, const char *detail)
{
    fprintf(stderr, "%s:%d: hiss! %s%s%s\n", src_name, line, msg,
            detail ? " " : "", detail ? detail : "");
    exit(1);
}

/* ---------------- lexer ---------------- */

enum {
    T_EOF, T_INT, T_STR, T_IDENT,
    T_ASSIGN, T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_COMMA,
};

static const char *src;
static int tok;              /* current token kind */
static int tok_int;          /* value for T_INT */
static char tok_text[256];   /* text for T_IDENT / T_STR */
static int tok_len;

static void next(void)
{
    for (;;) {
        while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') {
            if (*src == '\n')
                line++;
            src++;
        }
        if (*src == '~') {
            while (*src && *src != '\n')
                src++;
            continue;
        }
        break;
    }
    if (!*src) { tok = T_EOF; return; }

    if (*src >= '0' && *src <= '9') {
        tok_int = 0;
        while (*src >= '0' && *src <= '9')
            tok_int = tok_int * 10 + (*src++ - '0');
        tok = T_INT;
        return;
    }
    if ((*src >= 'a' && *src <= 'z') || (*src >= 'A' && *src <= 'Z') || *src == '_') {
        tok_len = 0;
        while ((*src >= 'a' && *src <= 'z') || (*src >= 'A' && *src <= 'Z') ||
               (*src >= '0' && *src <= '9') || *src == '_') {
            if (tok_len < 255)
                tok_text[tok_len++] = *src;
            src++;
        }
        tok_text[tok_len] = 0;
        tok = T_IDENT;
        return;
    }
    if (*src == '"') {
        src++;
        tok_len = 0;
        while (*src && *src != '"') {
            char c = *src++;
            if (c == '\\' && (*src == 'n' || *src == '"' || *src == '\\')) {
                c = (*src == 'n') ? '\n' : *src;
                src++;
            }
            if (c == '\n')
                die("string runs past end of line", NULL);
            if (tok_len < 255)
                tok_text[tok_len++] = c;
        }
        if (!*src)
            die("unterminated string", NULL);
        src++;
        tok_text[tok_len] = 0;
        tok = T_STR;
        return;
    }
    if (*src == '\'') {
        if (!src[1] || src[2] != '\'')
            die("bad character literal", NULL);
        tok_int = (unsigned char)src[1];
        src += 3;
        tok = T_INT;
        return;
    }

    switch (*src++) {
    case '=': if (*src == '=') { src++; tok = T_EQ; } else tok = T_ASSIGN; return;
    case '!': if (*src == '=') { src++; tok = T_NE; return; } die("stray '!'", NULL); return;
    case '<': if (*src == '=') { src++; tok = T_LE; } else tok = T_LT; return;
    case '>': if (*src == '=') { src++; tok = T_GE; } else tok = T_GT; return;
    case '+': tok = T_PLUS; return;
    case '-': tok = T_MINUS; return;
    case '*': tok = T_STAR; return;
    case '/': tok = T_SLASH; return;
    case '%': tok = T_PERCENT; return;
    case '(': tok = T_LPAREN; return;
    case ')': tok = T_RPAREN; return;
    case '{': tok = T_LBRACE; return;
    case '}': tok = T_RBRACE; return;
    case ',': tok = T_COMMA; return;
    }
    die("unexpected character", NULL);
}

static int is_word(const char *kw)
{
    return tok == T_IDENT && strcmp(tok_text, kw) == 0;
}

static void expect(int kind, const char *what)
{
    if (tok != kind)
        die("expected", what);
    next();
}

/* ---------------- code emitter ---------------- */

#define MAX_CODE  65536
#define MAX_STRS  128
#define MAX_VARS  64

static mu8 code[MAX_CODE];
static int codelen;
static char *strpool[MAX_STRS];
static mu16 strsize[MAX_STRS];
static int nstrs;
static char varname[MAX_VARS][64];
static int nvars;

static void emit(int b)
{
    if (codelen >= MAX_CODE)
        die("program too big", NULL);
    code[codelen++] = (mu8)b;
}

static void emit32(mu32 v)
{
    emit(v & 255); emit((v >> 8) & 255); emit((v >> 16) & 255); emit((v >> 24) & 255);
}

static void patch32(int at, mu32 v)
{
    code[at] = v & 255; code[at + 1] = (v >> 8) & 255;
    code[at + 2] = (v >> 16) & 255; code[at + 3] = (v >> 24) & 255;
}

static int intern(const char *s, int n)
{
    int i;
    for (i = 0; i < nstrs; i++)
        if (strsize[i] == n && memcmp(strpool[i], s, n) == 0)
            return i;
    if (nstrs >= MAX_STRS)
        die("too many strings", NULL);
    strpool[nstrs] = malloc(n ? n : 1);
    memcpy(strpool[nstrs], s, n);
    strsize[nstrs] = (mu16)n;
    return nstrs++;
}

static int var_find(const char *name)
{
    int i;
    for (i = 0; i < nvars; i++)
        if (strcmp(varname[i], name) == 0)
            return i;
    return -1;
}

static int var_slot(const char *name, int declare)
{
    int i = var_find(name);
    if (i >= 0)
        return i;
    if (!declare)
        die("unknown variable (declare it with purr):", name);
    if (nvars >= MAX_VARS)
        die("too many variables", NULL);
    i = (int)strlen(name);
    if (i > 63)
        i = 63;
    memcpy(varname[nvars], name, i);
    varname[nvars][i] = 0;
    return nvars++;
}

/* ---------------- parser / code generator ---------------- */

static void expr(void);

static const struct { const char *name; int value; } key_consts[] = {
    { "up", MEOW_KEY_UP }, { "down", MEOW_KEY_DOWN },
    { "left", MEOW_KEY_LEFT }, { "right", MEOW_KEY_RIGHT },
    { "space", ' ' }, { "esc", MEOW_KEY_ESC }, { "enter", MEOW_KEY_ENTER },
};

static void primary(void)
{
    int i;
    if (tok == T_INT) {
        emit(OP_PUSH); emit32((mu32)tok_int); next();
        return;
    }
    if (tok == T_LPAREN) {
        next(); expr(); expect(T_RPAREN, ")");
        return;
    }
    if (is_word("key")) {
        emit(OP_KEY); next();
        return;
    }
    if (is_word("random")) {
        next(); expect(T_LPAREN, "( after random");
        expr(); expect(T_RPAREN, ")");
        emit(OP_RAND);
        return;
    }
    if (tok == T_IDENT) {
        /* declared variables shadow the built-in key constants */
        i = var_find(tok_text);
        if (i >= 0) {
            emit(OP_LOAD); emit(i); next();
            return;
        }
        for (i = 0; i < (int)(sizeof key_consts / sizeof *key_consts); i++)
            if (strcmp(tok_text, key_consts[i].name) == 0) {
                emit(OP_PUSH); emit32((mu32)key_consts[i].value); next();
                return;
            }
        die("unknown variable (declare it with purr):", tok_text);
        return;
    }
    die("expected an expression", NULL);
}

static void unary(void)
{
    if (tok == T_MINUS) { next(); unary(); emit(OP_NEG); return; }
    if (is_word("not")) { next(); unary(); emit(OP_NOT); return; }
    primary();
}

static void muldiv(void)
{
    unary();
    while (tok == T_STAR || tok == T_SLASH || tok == T_PERCENT) {
        int op = tok == T_STAR ? OP_MUL : tok == T_SLASH ? OP_DIV : OP_MOD;
        next(); unary(); emit(op);
    }
}

static void addsub(void)
{
    muldiv();
    while (tok == T_PLUS || tok == T_MINUS) {
        int op = tok == T_PLUS ? OP_ADD : OP_SUB;
        next(); muldiv(); emit(op);
    }
}

static void comparison(void)
{
    addsub();
    if (tok == T_EQ || tok == T_NE || tok == T_LT || tok == T_GT ||
        tok == T_LE || tok == T_GE) {
        int op = tok == T_EQ ? OP_EQ : tok == T_NE ? OP_NE : tok == T_LT ? OP_LT
               : tok == T_GT ? OP_GT : tok == T_LE ? OP_LE : OP_GE;
        next(); addsub(); emit(op);
    }
}

static void and_expr(void)
{
    comparison();
    while (is_word("and")) { next(); comparison(); emit(OP_AND); }
}

static void expr(void)
{
    and_expr();
    while (is_word("or")) { next(); and_expr(); emit(OP_OR); }
}

/* break targets of the chase loop being compiled right now */
static int break_at[64];
static int nbreaks;

static void statement(void);

static void block(void)
{
    expect(T_LBRACE, "{");
    while (tok != T_RBRACE && tok != T_EOF)
        statement();
    expect(T_RBRACE, "}");
}

static void print_args(int newline)
{
    for (;;) {
        if (tok == T_STR) {
            emit(OP_PRINTS);
            int idx = intern(tok_text, tok_len);
            emit(idx & 255); emit(idx >> 8);
            next();
        } else {
            expr();
            emit(OP_PRINTI);
        }
        if (tok != T_COMMA)
            break;
        next();
    }
    if (newline) {
        emit(OP_PUSH); emit32('\n'); emit(OP_PRINTC);
    }
}

static void statement(void)
{
    if (is_word("purr")) {
        next();
        if (tok != T_IDENT)
            die("purr needs a variable name", NULL);
        int slot = var_slot(tok_text, 1);
        next(); expect(T_ASSIGN, "=");
        expr();
        emit(OP_STORE); emit(slot);
        return;
    }
    if (is_word("meow")) { next(); print_args(1); return; }
    if (is_word("mew"))  { next(); print_args(0); return; }
    if (is_word("pounce")) {
        next(); expr();
        emit(OP_JZ);
        int jz_at = codelen; emit32(0);
        block();
        if (is_word("hiss")) {
            next();
            emit(OP_JMP);
            int jmp_at = codelen; emit32(0);
            patch32(jz_at, (mu32)codelen);
            block();
            patch32(jmp_at, (mu32)codelen);
        } else {
            patch32(jz_at, (mu32)codelen);
        }
        return;
    }
    if (is_word("chase")) {
        int saved_breaks = nbreaks;
        next();
        int top = codelen;
        expr();
        emit(OP_JZ);
        int jz_at = codelen; emit32(0);
        block();
        emit(OP_JMP); emit32((mu32)top);
        patch32(jz_at, (mu32)codelen);
        while (nbreaks > saved_breaks)
            patch32(break_at[--nbreaks], (mu32)codelen);
        return;
    }
    if (is_word("scratch")) {
        next();
        if (nbreaks >= 64)
            die("too many scratches", NULL);
        emit(OP_JMP);
        break_at[nbreaks++] = codelen; emit32(0);
        return;
    }
    if (is_word("nap"))   { next(); expr(); emit(OP_NAP); return; }
    if (is_word("clear")) { next(); emit(OP_CLEAR); return; }
    if (is_word("color")) { next(); expr(); emit(OP_COLOR); return; }
    if (is_word("at")) {
        next(); expr(); expect(T_COMMA, ","); expr();
        emit(OP_AT);
        return;
    }
    if (tok == T_IDENT) {
        int slot = var_slot(tok_text, 0);
        next(); expect(T_ASSIGN, "=");
        expr();
        emit(OP_STORE); emit(slot);
        return;
    }
    die("expected a statement", NULL);
}

/* ---------------- driver ---------------- */

static char *read_file(const char *path, long *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "hiss! cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    if (fread(buf, 1, n, f) != (size_t)n) { fprintf(stderr, "hiss! cannot read %s\n", path); exit(1); }
    buf[n] = 0;
    fclose(f);
    if (out_len)
        *out_len = n;
    return buf;
}

static unsigned char *compile(const char *source, int *out_len)
{
    int i;
    src = source;
    next();
    while (tok != T_EOF)
        statement();
    emit(OP_HALT);

    int total = 6;
    for (i = 0; i < nstrs; i++)
        total += 2 + strsize[i];
    total += 4 + codelen;

    unsigned char *img = malloc(total), *p = img;
    memcpy(p, MBC_MAGIC, 4); p += 4;
    *p++ = nstrs & 255; *p++ = nstrs >> 8;
    for (i = 0; i < nstrs; i++) {
        *p++ = strsize[i] & 255; *p++ = strsize[i] >> 8;
        memcpy(p, strpool[i], strsize[i]); p += strsize[i];
    }
    *p++ = codelen & 255; *p++ = (codelen >> 8) & 255;
    *p++ = (codelen >> 16) & 255; *p++ = (codelen >> 24) & 255;
    memcpy(p, code, codelen);
    *out_len = total;
    return img;
}

static void usage(void)
{
    fprintf(stderr,
        "meowc — compiler for the meow language\n"
        "  meowc build game.meow -o game.mbc   compile to bytecode\n"
        "  meowc run game.meow                 compile and play here\n"
        "  meowc runbc game.mbc                play compiled bytecode\n");
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc < 3)
        usage();

    if (strcmp(argv[1], "runbc") == 0) {
        long n;
        src_name = argv[2];
        char *img = read_file(argv[2], &n);
        return host_run((unsigned char *)img, (int)n);
    }

    src_name = argv[2];
    char *source = read_file(argv[2], NULL);
    int img_len;
    unsigned char *img = compile(source, &img_len);

    if (strcmp(argv[1], "run") == 0)
        return host_run(img, img_len);

    if (strcmp(argv[1], "build") == 0) {
        const char *out = NULL;
        if (argc == 5 && strcmp(argv[3], "-o") == 0)
            out = argv[4];
        if (!out)
            usage();
        FILE *f = fopen(out, "wb");
        if (!f) { fprintf(stderr, "hiss! cannot write %s\n", out); return 1; }
        fwrite(img, 1, img_len, f);
        fclose(f);
        printf("purr: %s -> %s (%d bytes)\n", argv[2], out, img_len);
        return 0;
    }
    usage();
    return 1;
}
