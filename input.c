#include "input.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "shell.h"
#include "term.h"
#include "theme.h"
#include "util.h"

// A read can end in the middle of an escape sequence (a fast mouse wheel sends hundreds of
// 12-byte events; any chunk boundary cuts one). The cut-off head must not be handled alone,
// and the tail must not be taken for typed text - so an unfinished sequence waits for the rest.
#define IN_MAX   4096
#define IN_TAIL  48         // longest sequence we wait for
#define ESC_WAIT 0.04       // a lone Esc is a key press if nothing follows within this time

static char   pend[IN_TAIL + 8];
static size_t npend;
static double pend_t;

// where an unfinished escape sequence at the end of b[0..n) starts; n if the end is complete
static size_t unfinished_tail(const char *b, size_t n) {
    size_t lo = n > IN_TAIL ? n - IN_TAIL : 0;
    for (size_t i = n; i-- > lo;) {
        if (b[i] != 0x1b) continue;
        size_t len = n - i;
        if (len == 1) return i;                         // bare Esc: maybe the beginning of something
        if (b[i + 1] == 'O') return len >= 3 ? n : i;   // SS3: Esc O <final>
        if (b[i + 1] != '[') return n;                  // Alt+key: two bytes, complete
        size_t j = i + 2;
        if (j < n && b[j] == '[') j++;                  // Linux console F1-F5: Esc [ [ A
        for (; j < n; j++)
            if ((unsigned char)b[j] >= 0x40 && (unsigned char)b[j] <= 0x7e) return n;
        return i;                                       // parameters without the final byte
    }
    return n;
}

void input_poll(World *w, int *intro) {
    static char ibuf[IN_MAX + IN_TAIL + 8];
    char *buf = ibuf;
    size_t have = npend;
    memcpy(buf, pend, npend);
    size_t was = npend;
    npend = 0;
    int fresh = 0;
    while (have < IN_MAX) {                             // everything that has piled up, not just a piece
        ssize_t r = read(STDIN_FILENO, buf + have, IN_MAX - have);
        if (r <= 0) break;
        have += (size_t)r;
        fresh = 1;
    }
    if (have == 0) return;

    size_t cut = unfinished_tail(buf, have);
    if (cut < have) {
        double t = now_s();
        if (fresh || was == 0) pend_t = t;
        if (!fresh && t - pend_t > ESC_WAIT) cut = have;        // nothing came: it was a key
        else { npend = have - cut; memcpy(pend, buf + cut, npend); }
    }
    ssize_t n = (ssize_t)cut;
    if (n == 0) return;
    buf[n] = 0;

    if (shell_visible()) { shell_input(buf, (size_t)n); return; }     // the panel gets all the keys

    for (ssize_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)buf[i];

        if (ch == 0x1b && i + 2 < n && buf[i + 1] == '[' && buf[i + 2] == '<') {    //SGR-mouse
            int b, mx, my;
            char m;
            if (sscanf(buf + i + 3, "%d;%d;%d%c", &b, &mx, &my, &m) == 4 && m == 'M' && (b == 0 || b == 2)) {
                if (*intro) *intro = 0;
                else if (b == 0) world_drop_food_burst(w, mx - 0.5f, (my - 0.5f) * 2.0f);
                else world_kill_fish_at(w, mx - 0.5f, (my - 0.5f) * 2.0f);     // right button
            }
            char *e = strpbrk(buf + i + 3, "Mm");
            if (!e) break;
            i = e - buf;
            continue;
        }

        if (!*intro && ((ch == 0x1b && n - i >= 5 && !memcmp(buf + i, "\x1b[24~", 5)) || ch == 0x1d)) {   // F12, Ctrl-]
            i += ch == 0x1d ? 0 : 4;
            shell_toggle();
            if (shell_visible()) { shell_input(buf + i + 1, (size_t)(n - i - 1)); return; }
            continue;
        }

        if (ch == 0x1b && i + 1 < n && (buf[i + 1] == '[' || buf[i + 1] == 'O')) {
            // arrows, End, F-keys...: skip the whole sequence so its letters are not taken for hotkeys
            ssize_t j = i + 2;
            if (buf[i + 1] == '[') while (j < n && !(buf[j] >= 0x40 && buf[j] <= 0x7e)) j++;
            i = j;
            if (*intro) *intro = 0;
            continue;
        }

        if (*intro && ch != 'q' && ch != 'Q') { *intro = 0; continue; }     // any key closes intro

        switch (ch) {
            case 'q': case 'Q': term_quit = 1; break;
            case ' ': world_drop_food_burst(w, rndr(w->w * 0.2f, w->w * 0.8f), 1.5f); break;
            case 'f': case 'F': world_add_fish(w); break;
            case 'k': case 'K': world_kill_fish(w); break;
            case 't': theme_step(+1); break;
            case 'T': theme_step(-1); break;
            case 'b': case 'B': world_bubble_burst(w, 14); break;
            case 's': case 'S':
                shell_toggle();
                if (shell_visible()) { shell_input(buf + i + 1, (size_t)(n - i - 1)); return; }
                break;
        }
    }
}
