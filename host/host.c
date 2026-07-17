/* host/host.c — run meow bytecode in a normal terminal
 *
 * This is the "meow without the OS" experience: the same .mbc bytecode the
 * meow kernel runs also plays in a Linux terminal or a Windows console
 * (build meowc with MinGW and you get meowc.exe — .meow games on Windows).
 */
#include <stdio.h>
#include <stdlib.h>
#include "../vm/vm.h"

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#endif

static void term_puts_n(const char *s, int n)
{
    fwrite(s, 1, n, stdout);
    fflush(stdout);
}

static void term_clear(void)
{
    fputs("\x1b[2J\x1b[H", stdout);
    fflush(stdout);
}

static void term_at(int x, int y)
{
    /* clamp to the 80x25 meow screen, same as the kernel does */
    if (x < 0) x = 0;
    if (x > 79) x = 79;
    if (y < 0) y = 0;
    if (y > 24) y = 24;
    printf("\x1b[%d;%dH", y + 1, x + 1);
    fflush(stdout);
}

/* VGA color 0..15 -> ANSI foreground */
static void term_color(int c)
{
    static const int map[16] = { 30, 34, 32, 36, 31, 35, 33, 37,
                                 90, 94, 92, 96, 91, 95, 93, 97 };
    printf("\x1b[%dm", map[c & 15]);
    fflush(stdout);
}

static int term_rnd(int n)
{
    return rand() % n;
}

#ifdef _WIN32

static void term_nap(int ms) { Sleep(ms); }

static int term_key(void)
{
    if (!_kbhit())
        return 0;
    int c = _getch();
    if (c == 0 || c == 0xE0) {          /* extended key */
        int e = _getch();
        switch (e) {
        case 72: return MEOW_KEY_UP;
        case 80: return MEOW_KEY_DOWN;
        case 75: return MEOW_KEY_LEFT;
        case 77: return MEOW_KEY_RIGHT;
        }
        return 0;
    }
    if (c == '\r')
        return MEOW_KEY_ENTER;
    return c;
}

static void term_setup(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    srand((unsigned)GetTickCount());
}

static void term_restore(void) {}

#else /* POSIX */

static struct termios saved_tio;
static int tio_saved = 0;

static void term_nap(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int read_byte(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) == 1)
        return c;
    return -1;
}

static int term_key(void)
{
    int c = read_byte();
    if (c < 0)
        return 0;
    if (c == 27) {                       /* ESC: bare, or an arrow sequence */
        int b = read_byte();
        if (b < 0)
            return MEOW_KEY_ESC;
        if (b == '[') {
            int a = read_byte();
            switch (a) {
            case 'A': return MEOW_KEY_UP;
            case 'B': return MEOW_KEY_DOWN;
            case 'D': return MEOW_KEY_LEFT;
            case 'C': return MEOW_KEY_RIGHT;
            }
            return 0;
        }
        return MEOW_KEY_ESC;
    }
    if (c == '\r')
        return MEOW_KEY_ENTER;
    return c;
}

static void term_restore(void)
{
    if (tio_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
    fputs("\x1b[0m\x1b[?25h\n", stdout);  /* reset color, show cursor */
    fflush(stdout);
}

static void term_setup(void)
{
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved_tio) == 0) {
        struct termios raw = saved_tio;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        tio_saved = 1;
    }
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    srand((unsigned)(time(NULL) ^ getpid()));
    atexit(term_restore);
    fputs("\x1b[?25l", stdout);           /* hide cursor */
    fflush(stdout);
}

#endif

int host_run(const unsigned char *image, int len)
{
    static const vm_hooks hooks = {
        term_puts_n, term_clear, term_at, term_color,
        term_key, term_rnd, term_nap,
    };
    term_setup();
    int rc = vm_run(image, len, &hooks);
#ifdef _WIN32
    term_restore();
#endif
    if (rc < 0)
        fprintf(stderr, "hiss! bad bytecode (%d)\n", rc);
    return rc ? 1 : 0;
}
