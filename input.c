#include "input.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "term.h"

void input_poll(World *w, int *intro) {
    char buf[256];
    ssize_t n = read(STDIN_FILENO, buf, sizeof buf - 1);
    if (n <= 0) return;
    buf[n] = 0;

    for (ssize_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)buf[i];

        if (ch == 0x1b && i + 2 < n && buf[i + 1] == '[' && buf[i + 2] == '<') {    //SGR-mouse
            int b, mx, my;
            char m;
            if (sscanf(buf + i + 3, "%d;%d;%d%c", &b, &mx, &my, &m) == 4 && m == 'M' && b == 0) {
                if (*intro) *intro = 0;
                else world_drop_food_burst(w, mx - 0.5f, (my - 0.5f) * 2.0f);
            }
            char *e = strpbrk(buf + i + 3, "Mm");
            if (!e) break;
            i = e - buf;
            continue;
        }

        if (*intro && ch != 'q' && ch != 'Q') { *intro = 0; continue; }     // any key closes intro

        switch (ch) {
            case 'q': case 'Q': term_quit = 1; break;
            case ' ': world_drop_food_burst(w, rndr(w->w * 0.2f, w->w * 0.8f), 1.5f); break;
            case 'f': case 'F': world_add_fish(w); break;
            case 'b': case 'B': world_bubble_burst(w, 14); break;
        }
    }
}
