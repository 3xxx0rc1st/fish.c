#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "gfx.h"
#include "sprite.h"
#include "shell.h"
#include "theme.h"
#include "vt.h"

// start screen
#define BOX_W 38
#define BOX_H 17

static void intro_geom(int *bx, int *by) {
    *bx = (gfx_cols - BOX_W) / 2;
    *by = (gfx_rows - BOX_H) / 2;
    if (*bx < 0) *bx = 0;
    if (*by < 0) *by = 0;
}

static void intro_backdrop(const Theme *th) {
    for (int i = 0; i < gfx_w * gfx_h; i++) {
        gfx_px[i].r *= 0.5f; gfx_px[i].g *= 0.5; gfx_px[i].b *= 0.5f;
    }
    int bx, by;
    intro_geom(&bx, &by);
    for (int y = by * 2; y < (by + BOX_H) * 2 && y < gfx_h; y++)
        for (int x = bx; x < bx + BOX_W && x < gfx_w; x++)
            gfx_px[y * gfx_w + x] = th->ui_box;
}

static void hline(int x, int y, int w, const char *l, const char *m, const char *r, Col c) {
    char line[512];
    size_t n = 0, ll = strlen(l), ml = strlen(m), rl = strlen(r);
    if (w < 2 || (size_t)w * ml + ll + rl >= sizeof line) return;
    memcpy(line, l, ll); n += ll;
    for (int i = 0; i < w - 2; i++) { memcpy(line + n, m, ml); n +=ml; }
    memcpy(line + n, r, rl); n += rl;
    line[n] = 0;
    text_put(2, x, y, line, c, 1.0f);
}

static void intro_text(const World *w, const Theme *th) {
    int bx, by;
    intro_geom(&bx, &by);
    Col frame = th->ui_frame, title = th->ui_title, description = th->ui_sub, cr = th->ui_dim;
    Col key = th->ui_key, desc = th->ui_text;

    hline(bx, by, BOX_W, "┌", "─", "┐", frame);
    hline(bx, by + BOX_H - 1, BOX_W, "└", "─", "┘", frame);
    for (int y = by + 1; y < by + BOX_H - 1; y++) {
        text_put(2, bx, y, "│", frame, 1.0f);
        text_put(2, bx + BOX_W - 1, y, "│", frame, 1.0f);
    }

    const char *head = "FISH";
    text_put(2, bx + (BOX_W - utf8_len(head)) / 2, by + 1, head, title, 1.0f);
    const char *subhead = "A tiny ecosystem in your terminal.";
    text_put(2, bx + (BOX_W - utf8_len(subhead)) / 2, by + 2, subhead, description, 1.0f);

    // the Linux console has no mouse
    static const struct { const char *k, *kv, *d; } help[] = {
        {"space / click", "space", "feed the fish"},
        {"f", "f", "add a fish"},
        {"k / right-click", "k", "kill a fish"},
        {"t / T", "t / T", "switch palette"},
        {"b", "b", "launch bubbles"},
        {"s / F12", "s / F12", "shell overlay"},
        {"q", "q", "quit"},
    };
    for (int i = 0; i < 7; i++) {
        text_put(2, bx + 3, by + 4 + i, vt_active ? help[i].kv : help[i].k, key, 1.0f);
        text_put(2, bx + 3 + 16, by + 4 + i, help[i].d, desc, 1.0f);
    }

    const char *copyright = "Created by 3xxx0rc1st";
    text_put(2, bx + (BOX_W - utf8_len(copyright)) / 2, by + 12, copyright, cr, 1.0f);
    const char *year = vt_active ? "(c) 2026" : "© 2026";
    text_put(2, bx + (BOX_W - utf8_len(year)) / 2, by + 13, year, cr, 1.0f);

    const char *prompt = "press any key";
    float blink = 0.55f + 0.45f * sinf(world_phase(w, 3.0));   // blinking prompt
    text_put(2, bx + (BOX_W - utf8_len(prompt)) / 2, by + BOX_H - 2, prompt, th->ui_prompt, blink);
}

// system clock
static const char *clock_get(void) {
    static char clock[16] = "--:--:--";
    static time_t last_update = (time_t)-1;
    time_t now = time(NULL);
    if (now != last_update) {
        struct tm *tm = localtime(&now);
        if (tm)
            strftime(clock, sizeof clock, "%H:%M:%S", tm);
        else
            snprintf(clock, sizeof clock, "--:--:--");
        last_update = now;
    }
    return clock;
}

// regular overlays
static void overlays(const World *w, float alpha, float fps, const Theme *th) {
    int cols = gfx_cols, rows = gfx_rows;

    int sh = shell_visible();   // under the shell panel the water is quiet: no scrolling lines and tags

    // layers 0 and 1: scrolling lines move toward each other, one line each,
    // intersect and mix
    const char *s0 = "A Q U A R I U M";
    const char *s1 = "~ glub glub ~";
    float l0 = (float)strlen(s0), l1 = (float)strlen(s1);
    float x0 = (float)fmod(w->t * 2.5, cols + l0 + 10) - l0;
    float x1 = cols - (float)fmod(w->t * 4.0, cols + l1 + 10);
    if (!sh) {
        text_putf(0, x0, rows / 3, s0, col(200, 240, 255), 0.45f);
        text_putf(1, x1, rows / 3, s1, col(255, 230, 160), 0.60f);
    }

    // fish tags (layer 1)
    for (int i = 0; i < w->nfish; i++) {
        const Fish *f = &w->fish[i];
        if (f->dying || sh) continue;
        float fx = f->px + (f->x - f->px) * alpha;
        float fy = f->py + (f->y - f->py) * alpha;
        int len = (int)strlen(f->name);
        text_putf(1, fx - len * 0.5f, (int)floorf((fy - SPR_H * 0.5f - 2.0f) / 2.0f), f->name,
            col(255, 255, 255), 0.85f);
    }

    // HUD (layer 2): keys on the left, status on the right (dropped if it does not fit)
    char keys[96], stat[128];
    const char *clock = clock_get();
    if (sh)
        snprintf(keys, sizeof keys, " F12 / Ctrl-] hide shell | Shift-PgUp, wheel: history | exit: close ");
    else
        snprintf(keys, sizeof keys, " q quit | space feed | f +fish | k kill | t theme | b bubbles | s shell%s ",
                shell_running() ? "*" : "");
    snprintf(stat, sizeof stat, " %s | %d fish | %s | %2.0f fps ",
            clock, world_fish_alive(w), theme_name(theme_idx()), fps);
    text_put(2, 0, rows - 1, keys, th->ui_title, 0.90f);
    int sl = (int)strlen(stat);
    if (cols - sl > (int)strlen(keys))
        text_put(2, cols - sl, rows - 1, stat, th->ui_title, 0.90f);

    // palette name toast after a switch
    float age = theme_age();
    if (age < 2.2f && !sh) {
        char toast[64];
        snprintf(toast, sizeof toast, " palette: %s [%d/%d] ", theme_name(theme_idx()), theme_idx() + 1, theme_count());
        float a = clampf((2.2f - age) / 0.7f, 0, 1);
        text_put(2, (cols - (int)strlen(toast)) / 2, 2, toast, th->ui_title, a);
    }
    if (sh) shell_draw();
}

void ui_draw(const World *w, float alpha, int intro, float fps) {
    const Theme *th = theme_cur();
    text_clear();
    if (intro) {
        intro_backdrop(th);
        intro_text(w, th);
        return;
    }
    overlays(w, alpha, fps, th);
}
