#define _GNU_SOURCE
#include "shell.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gfx.h"
#include "term.h"
#include "theme.h"
#include "util.h"

// ======================= terminal emulator =======================

typedef struct { uint32_t ch, fg, bg; uint8_t at; } Cell;   // fg/bg: 0 - default, else C_SET | 0xRRGGBB
#define C_SET   0x1000000u
#define AT_BOLD 1
#define AT_DIM  2
#define AT_REV  4

#define HIST_MAX 2000

enum { ST_GROUND, ST_ESC, ST_CSI, ST_OSC, ST_CHARSET, ST_ESCINT };

typedef struct { int cx, cy; uint32_t fg, bg; uint8_t at; int gfx, ok; } Saved;

static struct {
    int cols, rows;
    Cell *scr, *alt, *cur;
    int in_alt;
    int cx, cy, wrap_pending;
    int top, bot;
    uint32_t fg, bg;
    uint8_t at;
    int app_cursor, autowrap, cursor_vis, gfx;
    Saved saved;
    int state, p[16], np, priv, inter;
    uint32_t cp;
    int utf_need;
    // scrollback
    Cell *hist;
    int hist_pos, hist_n, view_off;
} E;

static struct { int fd; pid_t pid; int visible; } S = { -1, 0, 0 };

static const uint32_t ANSI[16] = {
    0x1e2028, 0xe05561, 0x98c379, 0xe5c07b, 0x61afef, 0xc678dd, 0x56b6c2, 0xabb2bf,
    0x5c6370, 0xff6b7b, 0xb5e890, 0xffdf8d, 0x7ec8ff, 0xe08dff, 0x6fd8e5, 0xffffff,
};

static uint32_t palette256(int i) {
    if (i < 0) i = 0;
    if (i < 16) return C_SET | ANSI[i];
    if (i < 232) {
        static const int lv[6] = {0, 95, 135, 175, 215, 255};
        i -= 16;
        return C_SET | (uint32_t)(lv[i / 36] << 16 | lv[(i / 6) % 6] << 8 | lv[i % 6]);
    }
    int g = 8 + (i - 232) * 10;
    if (g > 255) g = 255;
    return C_SET | (uint32_t)(g << 16 | g << 8 | g);
}

static Col rgb(uint32_t v) { return col((float)((v >> 16) & 255), (float)((v >> 8) & 255), (float)(v & 255)); }

static Cell blank_cell(void) { Cell c = {' ', 0, E.bg, 0}; return c; }   // erase with the pen background
static Cell *row(int y) { return E.cur + (size_t)y * E.cols; }

static void clear_span(int y, int x0, int x1) {     // [x0, x1)
    Cell b = blank_cell();
    for (int x = x0; x < x1; x++) row(y)[x] = b;
}

static void hist_push(const Cell *line) {
    if (!E.hist) return;
    memcpy(E.hist + (size_t)E.hist_pos * E.cols, line, (size_t)E.cols * sizeof(Cell));
    E.hist_pos = (E.hist_pos + 1) % HIST_MAX;
    if (E.hist_n < HIST_MAX) E.hist_n++;
    if (E.view_off > 0 && E.view_off < E.hist_n) E.view_off++;     // keep the view still
}

static void scroll_up(int top, int bot, int n, int keep_hist) {
    int h = bot - top + 1;
    if (n > h) n = h;
    if (n <= 0) return;
    if (keep_hist && top == 0 && !E.in_alt)
        for (int i = 0; i < n; i++) hist_push(row(i));
    memmove(row(top), row(top + n), (size_t)(h - n) * E.cols * sizeof(Cell));
    for (int y = bot - n + 1; y <= bot; y++) clear_span(y, 0, E.cols);
}

static void scroll_down(int top, int bot, int n) {
    int h = bot - top + 1;
    if (n > h) n = h;
    if (n <= 0) return;
    memmove(row(top + n), row(top), (size_t)(h - n) * E.cols * sizeof(Cell));
    for (int y = top; y < top + n; y++) clear_span(y, 0, E.cols);
}

static void lf(void) {
    if (E.cy == E.bot) scroll_up(E.top, E.bot, 1, 1);
    else if (E.cy < E.rows - 1) E.cy++;
}

static void ri(void) {
    if (E.cy == E.top) scroll_down(E.top, E.bot, 1);
    else if (E.cy > 0) E.cy--;
}

static const uint16_t DEC_GFX[31] = {   // 0x60..0x7e of the DEC special graphics set
    0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B, 0x2518, 0x2510,
    0x250C, 0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C,
    0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
};

static void put(uint32_t cp) {
    if (cp >= 0x300 && cp <= 0x36F) return;                 // combining marks: no room for them
    if (E.wrap_pending) {
        if (E.autowrap) { E.cx = 0; lf(); }
        E.wrap_pending = 0;
    }
    if (E.gfx && cp >= 0x60 && cp <= 0x7e) cp = DEC_GFX[cp - 0x60];
    Cell *c = &row(E.cy)[E.cx];
    c->ch = cp; c->fg = E.fg; c->bg = E.bg; c->at = E.at;
    if (E.cx == E.cols - 1) E.wrap_pending = 1;
    else E.cx++;
}

static void reply(const char *s) {
    if (S.fd < 0) return;
    size_t n = strlen(s);
    ssize_t r = write(S.fd, s, n);
    (void)r;
}

static void pen_reset(void) { E.fg = E.bg = 0; E.at = 0; }

static void save_cursor(void) {
    E.saved = (Saved){E.cx, E.cy, E.fg, E.bg, E.at, E.gfx, 1};
}

static void restore_cursor(void) {
    if (!E.saved.ok) { E.cx = E.cy = 0; E.wrap_pending = 0; return; }
    E.cx = E.saved.cx < E.cols ? E.saved.cx : E.cols - 1;
    E.cy = E.saved.cy < E.rows ? E.saved.cy : E.rows - 1;
    E.fg = E.saved.fg; E.bg = E.saved.bg; E.at = E.saved.at; E.gfx = E.saved.gfx;
    E.wrap_pending = 0;
}

static void alt_screen(int on, int save) {
    if (on == E.in_alt) return;
    if (on) {
        if (save) save_cursor();
        E.in_alt = 1;
        E.cur = E.alt;
        Cell b = {' ', 0, 0, 0};
        for (int i = 0; i < E.rows * E.cols; i++) E.alt[i] = b;
    } else {
        E.in_alt = 0;
        E.cur = E.scr;
        if (save) restore_cursor();
    }
    E.top = 0; E.bot = E.rows - 1;
}

static void sgr(void) {
    if (E.np == 0) { pen_reset(); return; }
    for (int i = 0; i < E.np; i++) {
        int v = E.p[i];
        if (v == 0) pen_reset();
        else if (v == 1) E.at |= AT_BOLD;
        else if (v == 2) E.at |= AT_DIM;
        else if (v == 22) E.at &= (uint8_t)~(AT_BOLD | AT_DIM);
        else if (v == 7) E.at |= AT_REV;
        else if (v == 27) E.at &= (uint8_t)~AT_REV;
        else if (v >= 30 && v <= 37) E.fg = palette256(v - 30);
        else if (v == 39) E.fg = 0;
        else if (v >= 40 && v <= 47) E.bg = palette256(v - 40);
        else if (v == 49) E.bg = 0;
        else if (v >= 90 && v <= 97) E.fg = palette256(v - 90 + 8);
        else if (v >= 100 && v <= 107) E.bg = palette256(v - 100 + 8);
        else if (v == 38 || v == 48) {
            uint32_t c = 0;
            if (i + 2 < E.np && E.p[i + 1] == 5) { c = palette256(E.p[i + 2]); i += 2; }
            else if (i + 4 < E.np && E.p[i + 1] == 2) {
                c = C_SET | (uint32_t)((E.p[i + 2] & 255) << 16 | (E.p[i + 3] & 255) << 8 | (E.p[i + 4] & 255));
                i += 4;
            } else break;
            if (v == 38) E.fg = c; else E.bg = c;
        }
    }
}

static int PA(int i, int def) { return (i < E.np && E.p[i] > 0) ? E.p[i] : def; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void decset(int on) {
    for (int i = 0; i < E.np; i++) {
        switch (E.p[i]) {
            case 1: E.app_cursor = on; break;
            case 7: E.autowrap = on; break;
            case 25: E.cursor_vis = on; break;
            case 47: case 1047: alt_screen(on, 0); break;
            case 1048: if (on) save_cursor(); else restore_cursor(); break;
            case 1049: alt_screen(on, 1); break;
        }
    }
}

static void csi_dispatch(uint8_t f) {
    if (E.priv == '?') { if (f == 'h' || f == 'l') decset(f == 'h'); return; }
    if (E.priv == '>') { if (f == 'c') reply("\x1b[>0;276;0c"); return; }
    if (E.priv || E.inter) return;

    int n = PA(0, 1);
    switch (f) {
        case 'A': E.cy = clampi(E.cy - n, E.cy >= E.top ? E.top : 0, E.rows - 1); E.wrap_pending = 0; break;
        case 'B': E.cy = clampi(E.cy + n, 0, E.cy <= E.bot ? E.bot : E.rows - 1); E.wrap_pending = 0; break;
        case 'C': E.cx = clampi(E.cx + n, 0, E.cols - 1); E.wrap_pending = 0; break;
        case 'D': E.cx = clampi(E.cx - n, 0, E.cols - 1); E.wrap_pending = 0; break;
        case 'E': E.cx = 0; E.cy = clampi(E.cy + n, 0, E.rows - 1); E.wrap_pending = 0; break;
        case 'F': E.cx = 0; E.cy = clampi(E.cy - n, 0, E.rows - 1); E.wrap_pending = 0; break;
        case 'G': case '`': E.cx = clampi(n - 1, 0, E.cols - 1); E.wrap_pending = 0; break;
        case 'd': E.cy = clampi(n - 1, 0, E.rows - 1); E.wrap_pending = 0; break;
        case 'H': case 'f':
            E.cy = clampi(PA(0, 1) - 1, 0, E.rows - 1);
            E.cx = clampi(PA(1, 1) - 1, 0, E.cols - 1);
            E.wrap_pending = 0;
            break;
        case 'J': {
            int m = E.np ? E.p[0] : 0;
            if (m == 0) { clear_span(E.cy, E.cx, E.cols); for (int y = E.cy + 1; y < E.rows; y++) clear_span(y, 0, E.cols); }
            else if (m == 1) { for (int y = 0; y < E.cy; y++) clear_span(y, 0, E.cols); clear_span(E.cy, 0, E.cx + 1); }
            else if (m == 2 || m == 3) for (int y = 0; y < E.rows; y++) clear_span(y, 0, E.cols);
            break;
        }
        case 'K': {
            int m = E.np ? E.p[0] : 0;
            if (m == 0) clear_span(E.cy, E.cx, E.cols);
            else if (m == 1) clear_span(E.cy, 0, E.cx + 1);
            else if (m == 2) clear_span(E.cy, 0, E.cols);
            break;
        }
        case 'L': if (E.cy >= E.top && E.cy <= E.bot) scroll_down(E.cy, E.bot, n); break;
        case 'M': if (E.cy >= E.top && E.cy <= E.bot) scroll_up(E.cy, E.bot, n, 0); break;
        case 'S': scroll_up(E.top, E.bot, n, 1); break;
        case 'T': scroll_down(E.top, E.bot, n); break;
        case 'P': {
            n = clampi(n, 1, E.cols - E.cx);
            Cell *r = row(E.cy);
            memmove(r + E.cx, r + E.cx + n, (size_t)(E.cols - E.cx - n) * sizeof(Cell));
            clear_span(E.cy, E.cols - n, E.cols);
            break;
        }
        case '@': {
            n = clampi(n, 1, E.cols - E.cx);
            Cell *r = row(E.cy);
            memmove(r + E.cx + n, r + E.cx, (size_t)(E.cols - E.cx - n) * sizeof(Cell));
            clear_span(E.cy, E.cx, E.cx + n);
            break;
        }
        case 'X': clear_span(E.cy, E.cx, clampi(E.cx + n, 0, E.cols)); break;
        case 'm': sgr(); break;
        case 'r': {
            int t = PA(0, 1) - 1, b = PA(1, E.rows) - 1;
            if (b > E.rows - 1) b = E.rows - 1;
            if (t < b) { E.top = t; E.bot = b; }
            E.cx = E.cy = 0; E.wrap_pending = 0;
            break;
        }
        case 's': save_cursor(); break;
        case 'u': restore_cursor(); break;
        case 'n': {
            int m = E.np ? E.p[0] : 0;
            if (m == 5) reply("\x1b[0n");
            else if (m == 6) { char b[32]; snprintf(b, sizeof b, "\x1b[%d;%dR", E.cy + 1, E.cx + 1); reply(b); }
            break;
        }
        case 'c': if (!E.np || !E.p[0]) reply("\x1b[?62;c"); break;
    }
}

static void control(uint8_t c) {
    switch (c) {
        case 0x08: if (E.cx > 0) E.cx--; E.wrap_pending = 0; break;
        case 0x09: E.cx = clampi((E.cx / 8 + 1) * 8, 0, E.cols - 1); E.wrap_pending = 0; break;
        case 0x0a: case 0x0b: case 0x0c: lf(); E.wrap_pending = 0; break;
        case 0x0d: E.cx = 0; E.wrap_pending = 0; break;
    }
}

static void emu_reset(void) {
    E.state = ST_GROUND; E.utf_need = 0;
    E.cx = E.cy = E.wrap_pending = 0;
    E.top = 0; E.bot = E.rows - 1;
    pen_reset();
    E.app_cursor = 0; E.autowrap = 1; E.cursor_vis = 1; E.gfx = 0;
    E.saved.ok = 0;
    Cell b = {' ', 0, 0, 0};
    for (int i = 0; i < E.rows * E.cols; i++) E.scr[i] = E.alt[i] = b;
    E.in_alt = 0; E.cur = E.scr;
    E.hist_n = E.hist_pos = E.view_off = 0;
}

static void esc_byte(uint8_t c) {
    E.state = ST_GROUND;
    switch (c) {
        case '[': E.state = ST_CSI; E.np = 0; memset(E.p, 0, sizeof E.p); E.priv = 0; E.inter = 0; break;
        case ']': case 'P': case '_': case '^': case 'X': E.state = ST_OSC; break;
        case '(': E.state = ST_CHARSET; break;
        case ')': case '*': case '+': case ' ': case '#': case '%': E.state = ST_ESCINT; break;
        case '7': save_cursor(); break;
        case '8': restore_cursor(); break;
        case 'D': lf(); break;
        case 'E': E.cx = 0; lf(); break;
        case 'M': ri(); break;
        case 'c': emu_reset(); break;
        case 0x1b: E.state = ST_ESC; break;
    }
}

static void csi_byte(uint8_t c) {
    if (c < 0x20) { if (c == 0x1b) E.state = ST_ESC; else control(c); return; }
    if (c >= '0' && c <= '9') {
        if (E.np == 0) E.np = 1;
        int *p = &E.p[E.np - 1];
        if (*p < 100000) *p = *p * 10 + (c - '0');
    } else if (c == ';' || c == ':') {
        if (E.np == 0) E.np = 1;
        if (E.np < 16) E.np++;
    } else if (c >= 0x3c && c <= 0x3f) E.priv = c;
    else if (c >= 0x20 && c <= 0x2f) E.inter = c;
    else if (c >= 0x40 && c <= 0x7e) { E.state = ST_GROUND; csi_dispatch(c); }
}

static void feed_byte(uint8_t c) {
    switch (E.state) {
        case ST_ESC: esc_byte(c); return;
        case ST_CSI: csi_byte(c); return;
        case ST_OSC:                                   // title, hyperlinks, DCS...: ignored up to BEL / ST
            if (c == 0x07) E.state = ST_GROUND;
            else if (c == 0x1b) E.state = ST_ESC;
            return;
        case ST_CHARSET: E.gfx = (c == '0'); E.state = ST_GROUND; return;
        case ST_ESCINT: E.state = ST_GROUND; return;
    }
    // ground
    if (c < 0x20) { E.utf_need = 0; if (c == 0x1b) E.state = ST_ESC; else control(c); return; }
    if (c < 0x80) { E.utf_need = 0; if (c != 0x7f) put(c); return; }
    if (c >= 0xC2 && c <= 0xF4) {
        E.utf_need = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        E.cp = c & (c >= 0xF0 ? 0x07 : c >= 0xE0 ? 0x0F : 0x1F);
    } else if (c >= 0x80 && c < 0xC0 && E.utf_need > 0) {
        E.cp = (E.cp << 6) | (c & 0x3F);
        if (--E.utf_need == 0) put(E.cp);
    } else E.utf_need = 0;
}

static void emu_feed(const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) feed_byte((uint8_t)b[i]);
}

static void emu_resize(int cols, int rows) {
    Cell b = {' ', 0, 0, 0};
    Cell *ns = malloc((size_t)cols * rows * sizeof(Cell));
    Cell *na = malloc((size_t)cols * rows * sizeof(Cell));
    for (int i = 0; i < cols * rows; i++) ns[i] = na[i] = b;
    int shift = 0;
    if (E.scr) {
        if (E.cy >= rows) shift = E.cy - rows + 1;     // keep the cursor line: drop lines from the top
        for (int y = 0; y < rows; y++) {
            int sy = y + shift;
            if (sy >= E.rows) break;
            memcpy(ns + (size_t)y * cols, E.scr + (size_t)sy * E.cols, (size_t)(cols < E.cols ? cols : E.cols) * sizeof(Cell));
        }
    }
    if (!E.hist || E.cols != cols) {                    // history rows have the old width: drop them
        free(E.hist);
        E.hist = malloc((size_t)HIST_MAX * cols * sizeof(Cell));
        E.hist_n = E.hist_pos = 0;
    }
    E.view_off = 0;
    free(E.scr); free(E.alt);
    E.scr = ns; E.alt = na;
    E.cur = E.in_alt ? E.alt : E.scr;
    E.cy = clampi(E.cy - shift, 0, rows - 1);
    E.cx = clampi(E.cx, 0, cols - 1);
    E.cols = cols; E.rows = rows;
    E.top = 0; E.bot = rows - 1;
    E.wrap_pending = 0;
}

// ======================= pty and panel =======================

#define MIN_COLS 40
#define MIN_ROWS 12

static void layout(int *x0, int *y0, int *pw, int *ph) {
    *pw = gfx_cols - 2; if (*pw > 134) *pw = 134;
    *ph = gfx_rows - 2; if (*ph > 42) *ph = 42;
    *x0 = (gfx_cols - *pw) / 2;
    *y0 = (gfx_rows - *ph) / 2;
}

static void grid_size(int *c, int *r) {
    int x0, y0, pw, ph;
    layout(&x0, &y0, &pw, &ph);
    *c = pw - 2; *r = ph - 2;
}

int shell_visible(void) { return S.visible; }
int shell_running(void) { return S.fd >= 0; }
int shell_fd(void) { return S.fd; }

static int spawn(int gc, int gr) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0) return 0;
    char sn[128];
    if (grantpt(m) < 0 || unlockpt(m) < 0 || ptsname_r(m, sn, sizeof sn) != 0) { close(m); return 0; }
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_row = (unsigned short)gr; ws.ws_col = (unsigned short)gc;
    ioctl(m, TIOCSWINSZ, &ws);
    fcntl(m, F_SETFD, FD_CLOEXEC);
    fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) { close(m); return 0; }
    if (pid == 0) {
        setsid();
        int s = open(sn, O_RDWR);
        if (s < 0) _exit(127);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0); dup2(s, 1); dup2(s, 2);
        if (s > 2) close(s);
        for (int sg = 1; sg < 32; sg++) if (sg != SIGKILL && sg != SIGSTOP) signal(sg, SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("CFISH_AQUARIUM", "1", 1);
        unsetenv("LINES"); unsetenv("COLUMNS");
        const char *sh = getenv("SHELL");
        if (!sh || !*sh) sh = "/bin/sh";
        execl(sh, sh, (char *)NULL);
        execl("/bin/sh", "sh", (char *)NULL);
        _exit(127);
    }
    S.fd = m;
    S.pid = pid;
    E.saved.ok = 0;
    emu_resize(gc, gr);
    emu_reset();
    return 1;
}

static void hide(void) {
    if (!S.visible) return;
    S.visible = 0;
    term_shell_mode(0);
}

static void reap(int wait_a_bit) {
    if (S.pid <= 0) return;
    for (int i = 0; i < (wait_a_bit ? 20 : 1); i++) {
        if (waitpid(S.pid, NULL, WNOHANG) == S.pid) { S.pid = 0; return; }
        if (wait_a_bit) usleep(10000);
    }
    if (wait_a_bit) { kill(S.pid, SIGKILL); waitpid(S.pid, NULL, 0); S.pid = 0; }
}

static void child_gone(void) {
    if (S.fd >= 0) close(S.fd);
    S.fd = -1;
    reap(1);
    hide();
}

void shell_shutdown(void) {
    if (S.fd >= 0) { close(S.fd); S.fd = -1; }       // the kernel hangs up the child's terminal
    if (S.pid > 0) {
        kill(S.pid, SIGHUP);
        kill(S.pid, SIGCONT);
        waitpid(S.pid, NULL, WNOHANG);
        S.pid = 0;
    }
    S.visible = 0;
}

void shell_toggle(void) {
    if (S.visible) { hide(); return; }
    if (gfx_cols < MIN_COLS || gfx_rows < MIN_ROWS) return;
    int gc, gr;
    grid_size(&gc, &gr);
    if (S.fd < 0 && !spawn(gc, gr)) return;
    S.visible = 1;
    term_shell_mode(1);
    shell_resize();
}

void shell_resize(void) {
    if (S.fd < 0) return;
    if (gfx_cols < MIN_COLS || gfx_rows < MIN_ROWS) { hide(); return; }
    int gc, gr;
    grid_size(&gc, &gr);
    if (gc == E.cols && gr == E.rows) return;
    emu_resize(gc, gr);
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_row = (unsigned short)gr; ws.ws_col = (unsigned short)gc;
    ioctl(S.fd, TIOCSWINSZ, &ws);                    // the kernel sends SIGWINCH to the foreground job
}

void shell_pump(void) {
    if (S.fd < 0) return;
    char buf[4096];
    for (int k = 0; k < 16; k++) {
        ssize_t n = read(S.fd, buf, sizeof buf);
        if (n > 0) { emu_feed(buf, (size_t)n); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
        child_gone();                                // EOF / EIO: the shell has exited
        break;
    }
}

static void to_pty(const char *b, size_t n) {
    while (n > 0 && S.fd >= 0) {
        ssize_t w = write(S.fd, b, n);
        if (w > 0) { b += w; n -= (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && errno == EAGAIN) { usleep(1000); continue; }
        break;
    }
}

static void scroll_view(int d) {
    if (E.in_alt) return;
    E.view_off = clampi(E.view_off + d, 0, E.hist_n);
}

// b must be NUL-terminated after n bytes (input_poll does this)
void shell_input(const char *b, size_t n) {
    char out[512];
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)b[i];
        if (c == 0x1d) { if (o) to_pty(out, o); hide(); return; }                // Ctrl-]
        if (c == 0x1b && i + 1 < n && b[i + 1] == '[') {
            if (i + 2 < n && b[i + 2] == '<') {                                  // mouse: the wheel scrolls history
                int bt = -1, mx, my;
                char m;
                if (sscanf(b + i + 3, "%d;%d;%d%c", &bt, &mx, &my, &m) == 4 && m == 'M') {
                    if (bt == 64) scroll_view(3);
                    else if (bt == 65) scroll_view(-3);
                }
                size_t j = i + 3;
                while (j < n && b[j] != 'M' && b[j] != 'm') j++;
                i = j;
                continue;
            }
            if (n - i >= 5 && !memcmp(b + i, "\x1b[24~", 5)) { if (o) to_pty(out, o); hide(); return; }   // F12
            if (n - i >= 6 && !memcmp(b + i, "\x1b[5;2~", 6)) { scroll_view(E.rows / 2); i += 5; continue; }
            if (n - i >= 6 && !memcmp(b + i, "\x1b[6;2~", 6)) { scroll_view(-E.rows / 2); i += 5; continue; }
            if (E.app_cursor && i + 2 < n && strchr("ABCDHF", b[i + 2])) {      // application cursor keys
                if (o + 3 > sizeof out) { to_pty(out, o); o = 0; }
                out[o++] = 0x1b; out[o++] = 'O'; out[o++] = b[i + 2];
                i += 2;
                E.view_off = 0;
                continue;
            }
        }
        E.view_off = 0;                                                          // typing returns to the live screen
        if (o == sizeof out) { to_pty(out, o); o = 0; }
        out[o++] = (char)c;
    }
    if (o) to_pty(out, o);
}

static const Cell *view_row(int d) {        // screen line d as seen with the scrollback offset
    if (E.view_off == 0) return E.cur + (size_t)d * E.cols;
    int ci = E.hist_n - E.view_off + d;     // index in "history + screen"
    if (ci < E.hist_n) return E.hist + (size_t)((E.hist_pos - E.hist_n + ci + HIST_MAX) % HIST_MAX) * E.cols;
    return E.scr + (size_t)(ci - E.hist_n) * E.cols;
}

void shell_draw(void) {
    if (!S.visible || S.fd < 0) return;
    const Theme *th = theme_cur();
    int x0, y0, pw, ph;
    layout(&x0, &y0, &pw, &ph);

    // glass: the water shows through, but dimmed
    for (int y = y0 * 2; y < (y0 + ph) * 2; y++)
        for (int x = x0; x < x0 + pw; x++) gfx_blend(x, y, th->ui_box, 0.74f);

    // frame
    Col fr = th->ui_frame;
    text_put(2, x0, y0, "┌", fr, 1.0f);
    text_put(2, x0 + pw - 1, y0, "┐", fr, 1.0f);
    text_put(2, x0, y0 + ph - 1, "└", fr, 1.0f);
    text_put(2, x0 + pw - 1, y0 + ph - 1, "┘", fr, 1.0f);
    char title[64];
    if (E.view_off) snprintf(title, sizeof title, " shell  [history -%d] ", E.view_off);
    else snprintf(title, sizeof title, " shell ");
    int tl = (int)strlen(title);
    for (int x = x0 + 1; x < x0 + pw - 1; x++) {
        if (x < x0 + 2 || x >= x0 + 2 + tl) text_put(2, x, y0, "─", fr, 1.0f);   // glass shows through the title's spaces
        text_put(2, x, y0 + ph - 1, "─", fr, 1.0f);
    }
    for (int y = y0 + 1; y < y0 + ph - 1; y++) {
        text_put(2, x0, y, "│", fr, 1.0f);
        text_put(2, x0 + pw - 1, y, "│", fr, 1.0f);
    }
    text_put(2, x0 + 2, y0, title, th->ui_title, 1.0f);

    int blink = fmodf((float)now_s(), 1.1f) < 0.65f;
    Col deffg = th->ui_text;
    for (int r = 0; r < E.rows; r++) {
        const Cell *cells = view_row(r);
        for (int c = 0; c < E.cols; c++) {
            const Cell *e = &cells[c];
            int has_bg = e->bg != 0;
            Col F = e->fg ? rgb(e->fg) : deffg;
            Col B = has_bg ? rgb(e->bg) : th->ui_box;
            if (e->at & AT_REV) { Col t = F; F = B; B = t; has_bg = 1; }
            if (e->at & AT_BOLD) F = scalec(F, 1.15f);
            float a = (e->at & AT_DIM) ? 0.6f : 1.0f;
            int px = x0 + 1 + c, py = y0 + 1 + r;
            if (E.view_off == 0 && E.cursor_vis && blink && r == E.cy && c == E.cx) {
                B = th->ui_key; F = th->ui_box; has_bg = 1; a = 1.0f;
            }
            if (has_bg) {
                gfx_blend(px, py * 2, B, 0.92f);
                gfx_blend(px, py * 2 + 1, B, 0.92f);
            }
            if (e->ch > ' ') text_cell(2, px, py, e->ch, F, a);
        }
    }
}
