#define _POSIX_C_SOURCE 200809L
#include "term.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

volatile sig_atomic_t term_quit = 0;
volatile sig_atomic_t term_resized = 1;
int term_cols = 80, term_rows = 24;

static struct termios old_tio;
static int raw_on = 0;

void term_restore(void) {
    if (!raw_on) return;
    const char *s = "\x1b[?2026l\x1b[?1006l\x1b[?1000l\x1b[?7h\x1b[0m\x1b[?25h\x1b[?1049l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s));
    (void)r;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_tio);
    raw_on = 0;
}

static void on_sig(int s) {
    if (s == SIGWINCH) term_resized = 1;
    else term_quit = 1;
}

static void on_crash(int s) {   // SA_RESETHAND: repeated raise kills fish after restore
    term_restore();
    raise(s);
}

void term_init(void) {
    tcgetattr(STDIN_FILENO, &old_tio);
    struct termios t = old_tio;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);    // keep ISIG: handle Ctrl-C manually
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    raw_on = 1;
    atexit(term_restore);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_sig;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = on_crash;
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);

    // alt-screen, cursor off, word wrap off, SGR mouse
    const char *s = "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[?1000h\x1b[?1006h\x1b[2J";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s));
    (void)r;
}

void term_size(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        term_cols = w.ws_col;
        term_rows = w.ws_row;
    } else {
        term_cols = 80;
        term_rows = 24;
    }
}

// input buffer
static char *ob = NULL;
static size_t ob_len = 0, ob_cap = 0;

static void ob_need(size_t n) {
    if (ob_len + n > ob_cap) {
        ob_cap = (ob_len + n) * 2 + 4096;
        ob = realloc(ob, ob_cap);
        if (!ob) abort();
    }
}

void ob_reset(void) { ob_len = 0; }

void ob_str(const char *s, size_t n) {
    ob_need(n);
    memcpy(ob + ob_len, s, n);
    ob_len += n;
}

void ob_c(char c) {
    ob_need(1);
    ob[ob_len++] = c;
}

void ob_num(unsigned v) {
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    ob_need((size_t)n);
    while (n) ob[ob_len++] = t[--n];
}

void ob_cp(uint32_t c) {
    char b[4];
    size_t n;
    if (c < 0x80)         { b[0] = (char)c; n = 1; }
    else if (c < 0x800)   { b[0] = (char)(0xC0 | (c >> 6)); b[1] = (char)(0x80 | (c & 63)); n = 2; }
    else if (c < 0x10000) { b[0] = (char)(0xE0 | (c >> 12)); b[1] = (char)(0x80 | ((c >> 6) & 63)); b[2] = (char)(0x80 | (c & 63)); n = 3; }
    else                  { b[0] = (char)(0xF0 | (c >> 18)); b[1] = (char)(0x80 | ((c >> 12) & 63)); b[2] = (char)(0x80 | ((c >> 6) & 63)); b[3] = (char)(0x80 | (c & 63)); n = 4; }
    ob_str(b, n);
}

void ob_flush(void) {
    size_t off = 0;
    while (off < ob_len) {
        ssize_t w = write(STDOUT_FILENO, ob + off, ob_len - off);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        off += (size_t)w;
    }
    ob_len = 0;
}
