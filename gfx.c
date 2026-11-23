#include "gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "term.h"

#define QMASK 0xFC  // color quanting: less "junk" in diff

typedef struct { uint32_t ch; uint8_t fr, fg, fb, br, bg, bb; uint8_t pad[2]; } Cell;
typedef struct { uint32_t ch; Col c; float a; } TCell;

int gfx_cols, gfx_rows, gfx_w, gfx_h;
Col *gfx_px = NULL;

static Cell *front = NULL, *back = NULL;
static TCell *tl[GFX_NTL];

void gfx_resize(int cols, int rows) {
    free(gfx_px); free(front); free(back);
    for (int i = 0; i < GFX_NTL; i++) free(tl[i]);

    gfx_cols = cols;
    gfx_rows = rows;
    gfx_w = cols;
    gfx_h = rows * 2;
    size_t n = (size_t)cols * rows;

    gfx_px = calloc((size_t)gfx_w * gfx_h, sizeof(Col));
    front = calloc(n, sizeof(Cell));
    back = calloc(n, sizeof(Cell));
    for (int i = 0; i < GFX_NTL; i++) tl[i] = calloc(n, sizeof(TCell));
    if (!gfx_px || !front || !back) abort();
    for (size_t i = 0; i < n; i++) front[i].ch = 0xFFFFFFFFu;   // force full redraw

    OB_LIT("\x1b[0m\x1b[2J");
    ob_flush();
}

void gfx_splat(float x, float y, Col c, float a) {
    float fx = x - 0.5f, fy = y - 0.5f;
    int ix = (int)floorf(fx), iy = (int)floorf(fy);
    float tx = fx - ix, ty = fy - iy;
    gfx_blend(ix,     iy,     c, a * (1 - tx) * (1 - ty));
    gfx_blend(ix + 1, iy,     c, a * tx * (1 - ty));
    gfx_blend(ix,     iy + 1, c, a * (1 - tx) * ty);
    gfx_blend(ix + 1, iy + 1, c, a * tx * ty);
}

// text
uint32_t utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = *s++;
    int n = 0;
    if (c >= 0xF0) { c &= 7; n = 3; }
    else if (c >= 0xE0) { c &= 15; n = 2; }
    else if (c >= 0xC0) { c &= 31; n = 1; }
    while (n-- > 0 && (*s & 0xC0) == 0x80) c = (c <<6) | (*s++ & 63);
    *p = (const char *)s;
    return c;
}

int utf8_len(const char *s) {
    int n = 0;
    while (*s) { utf8_next(&s); n++; }
    return n;
}

void text_clear(void) {
    for (int i = 0; i < GFX_NTL; i++) memset(tl[i], 0, (size_t)gfx_cols * gfx_rows * sizeof(TCell));
}

void text_put(int layer, int x, int y, const char *s, Col c, float a) {
    if (a < 0.02f || y < 0 || y >= gfx_rows) return;
    while (*s) {
        uint32_t cp = utf8_next(&s);
        if (x >= gfx_cols) break;
        if (x >= 0 && cp != ' ') {
            TCell *t = &tl[layer][y * gfx_cols + x];
            if (t->ch) { t->c = lerpc(t->c, c, a); t->a = 1 - (1 - t->a) * (1 - a); }
            else  { t->c = c; t->a = a; }
            t->ch = cp;
        }
        x++;
    }
}

void text_putf(int layer, float xf, int y, const char *s, Col c, float a) {
    int xi = (int)floorf(xf);
    float fr = xf - xi;
    text_put(layer, xi, y, s, c, a * (1 - fr));
    text_put(layer, xi + 1, y, s, c, a * fr);
}

// present
static inline uint8_t q8(float v) {
    int i = (int){v + 0.5f};
    if (i < 0) i = 0;
    if (i > 255) i =255;
    return (uint8_t)(i & QMASK);
}

void gfx_present(void) {
    // 1. canvas + text -> back[]
    for (int y = 0; y < gfx_rows; y++)
        for (int x = 0; x < gfx_cols; x++) {
            int i = y * gfx_cols + x;
            Col top = gfx_px[(2 * y) * gfx_w + x], bot = gfx_px[(2 * y + 1) * gfx_w + x];
            Cell c;
            memset(&c, 0, sizeof c);

            TCell out;
            memset(&out, 0, sizeof out);
            for (int L = 0; L < GFX_NTL; L++) {
                const TCell *t = &tl[L][i];
                if (!t->ch) continue;
                if (out.ch) { out.c = lerpc(out.c, t->c, t->a); out.a = 1 - (1 - out.a) * (1 - t->a); }
                else out = *t;
                out.ch = t-> ch;
            }

            if (out.ch) {
                Col bg = lerpc(top, bot, 0.5f);
                Col fg = lerpc(bg, out.c, clampf(out.a, 0, 1));
                c.ch = out.ch;
                c.fr = q8(fg.r); c.fg = q8(fg.g); c.fb = q8(fg.b);
                c.br = q8(bg.r); c.bg = q8(bg.g); c.bb = q8(bg.b);
            } else {
                uint8_t tr = q8(top.r), tg = q8(top.g), tb = q8(top.b);
                uint8_t br = q8(bot.r), bgc = q8(bot.g), bb = q8(bot.b);
                if (tr == br && tg == bgc && tb == bb) {
                    c.ch = ' ';
                    c.br = tr; c.bg = tg; c.bb = tb;
                } else {
                    c.ch = 0x2580;  // half-block
                    c.fr = tr; c.fg = tg; c.fb = tb;
                    c.br = br; c.bg = bgc; c.bb =bb;
                }
            }
            back[i] = c;
        }

    // 2. diff with front[] -> escape-sequences
    ob_reset();
    OB_LIT("\x1b[?2026h");   // synchronized update: without frame rips
    int cx = -1, cy = -1;
    int fgv = 0, bgv = 0;
    uint8_t cfr = 0, cfg = 0, cfb = 0, cbr = 0, cbg = 0, cbb = 0;
    for (int y = 0; y < gfx_rows; y++)
        for (int x = 0; x < gfx_cols; x++) {
            int i = y * gfx_cols + x;
            if (!memcmp(&back[i], &front[i], sizeof(Cell))) continue;
            const Cell *c = &back[i];
            if (cy != y || cx != x) {
                OB_LIT("\x1b[");
                ob_num((unsigned)y + 1); ob_c(';'); ob_num((unsigned)x + 1); ob_c('H');
            }
            if (c->ch != ' ' && (!fgv || cfr != c->fr || cfg != c->fg || cfb != c->fb)) {
                OB_LIT("\x1b[38;2;");
                ob_num(c->fr); ob_c(';'); ob_num(c->fg); ob_c(';'); ob_num(c->fb); ob_c('m');
                cfr = c->fr; cfg = c->fg; cfb = c->fb; fgv = 1;
            }
            if (!bgv || cbr != c->br || cbg != c->bg || cbb != c->bb) {
                OB_LIT("\x1b[48;2;");
                ob_num(c->br); ob_c(';'); ob_num(c->bg); ob_c(';'); ob_num(c->bb); ob_c('m');
                cbr = c->br; cbg = c->bg; cbb = c->bb; bgv = 1;
            }
            ob_cp(c->ch);
            cx = x + 1;
            cy = y;
        }
    OB_LIT("\x1b[?2026l");
    memcpy(front, back, (size_t)gfx_cols * gfx_rows * sizeof(Cell));
    ob_flush();
}