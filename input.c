#include "input.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "shell.h"
#include "term.h"
#include "theme.h"

void input_poll(World *w, int *intro) {
    char buf[256];
    ssize_t n = read(STDIN_FILENO, buf, sizeof buf - 1);
    if (n <= 0) return;
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
