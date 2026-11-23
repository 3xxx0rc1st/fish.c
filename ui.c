#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "sprite.h"

// start screen
#define BOX_W 36
#define BOX_H 10

static void intro_geom(int *bx, int *by) {
    *bx = (gfx_cols - BOX_W) / 2;
    *by = (gfx_rows - BOX_H) / 2;
    if (*bx < 0) *bx = 0;
    if (*by < 0) *by = 0;
}

static void intro_backdrop(void) {
    for (int i = 0; i < gfx_w * gfx_h; i++) {
        gfx_px[i].r *= 0.5f; gfx_px[i].g *= 0.5; gfx_px[i].b *= 0.5f;
    }
    int bx, by;
    intro_geom(&bx, &by);
    for (int y = by * 2; y < (by + BOX_H) * 2 && y < gfx_h; y++)
        for (int x = bx; x < bx + BOX_W && x < gfx_w; x++)
            gfx_px[y * gfx_w + x] = col(6, 18, 36);
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

static void intro_text(float t) {
    int bx, by;
    intro_geom(&bx, &by);
    Col frame = col(120, 210, 235), title = col(255, 255, 255);
    Col key = col(255, 215, 110), desc = col(215, 232, 245);

    hline(bx, by, BOX_W, "┌", "─", "┐", frame);
    hline(bx, by + BOX_H - 1, BOX_W, "└", "─", "┘", frame);
    for (int y = by + 1; y < by + BOX_H - 1; y++) {
        text_put(2, bx, y, "│", frame, 1.0f);
        text_put(2, bx + BOX_W - 1, y, "│", frame, 1.0f);
    }

    const char *head = "TERMINAL AQUARIUM";
    text_put(2, bx + (BOX_W - utf8_len(head)) / 2, by + 1, head, title, 1.0f);

    static const struct { const char *k, *d; } help[] = {
        {"space / click", "feed the fish"},
        {"f", "add a fish"},
        {"b", "launch bubbles"},
        {"q", "quit"},
    };
    for (int i = 0; i < 4; i++) {
        text_put(2, bx + 3, by + 3 + i, help[i].k, key, 1.0f);
        text_put(2, bx + 3 + 16, by + 3 + i, help[i].d, desc, 1.0f);
    }

    const char *prompt = "press any key";
    float blink = 0.55f + 0.45f * sinf(t * 3.0f);   // blinking prompt
    text_put(2, bx + (BOX_W - utf8_len(prompt)) / 2, by + BOX_H - 2, prompt, col(150, 230, 255), blink);
}

// regular overlays
static void overlays(const World *w, float alpha, float fps) {
    int cols = gfx_cols, rows = gfx_rows;

    // layers 0 and 1: scrolling lines move toward each other, one line each,
    // intersect and mix
    const char *s0 = "A Q U A R I U M";
    const char *s1 = "~ glub glub ~";
    float l0 = (float)strlen(s0), l1 = (float)strlen(s1);
    float x0 = fmodf(w->t * 2.5f, cols + l0 + 10) - l0;
    float x1 = cols - fmodf(w->t * 4.0f, cols + l1 + 10);
    text_putf(0, x0, rows / 3, s0, col(200, 240, 255), 0.45f);
    text_putf(1, x1, rows / 3, s1, col(255, 230, 160), 0.60f);

    // fish tags (layer 1)
    for (int i = 0; i < w->nfish; i++) {
        const Fish *f = &w->fish[i];
        float fx = f->px + (f->x - f->px) * alpha;
        float fy = f->py + (f->y - f->py) * alpha;
        int len = (int)strlen(f->name);
        text_putf(1, fx - len * 0.5f, (int)floorf((fy - SPR_H * 0.5f - 2.0f) / 2.0f), f->name,
            col(255, 255, 255), 0.85f);
    }

    // HUD (layer 2)
    char hud[160];
    snprintf(hud, sizeof hud, " q quit | space/click feed | f fish | b bubbles | %2.0f fps ", fps);
    text_put(2, 0, rows - 1, hud, col(255, 255, 255), 0.90f);
}

void ui_draw(const World *w, float alpha, int intro, float fps) {
    text_clear();
    if (intro) {
        intro_backdrop();
        intro_text(w->t);
        return;
    }
    overlays(w, alpha, fps);
}
