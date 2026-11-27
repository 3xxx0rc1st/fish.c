#include "gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "kgp.h"
#include "term.h"
#include "vt.h"

#define QMASK 0xFC  // color quanting: less "junk" in diff

typedef struct { uint32_t ch; uint8_t fr, fg, fb, br, bg, bb; uint8_t pad[2]; } Cell;
typedef struct { uint32_t ch; Col c; float a; } TCell;

int gfx_cols, gfx_rows, gfx_w, gfx_h;
int gfx_tol = 0;    // truecolor diff tolerance (0 = exact), see gfx.h
Col *gfx_px = NULL;

static Cell *front = NULL, *back = NULL;
static TCell *tl[GFX_NTL];
static uint8_t *trow;      // trow[y] != 0: some text layer has a cell in row y (most rows are empty)

void gfx_resize(int cols, int rows) {
    free(gfx_px); free(front); free(back); free(trow);
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
    trow = calloc((size_t)rows, 1);
    if (!gfx_px || !front || !back || !trow) abort();
    // force full redraw; with the kitty image underneath a cleared screen already is the blank state
    for (size_t i = 0; i < n; i++) front[i].ch = kgp_active && !vt_active ? ' ' : 0xFFFFFFFFu;

    OB_LIT("\x1b[0m\x1b[2J");
    kgp_emit_clear();       // an image of the old size must not stay (no-op without the backend)
    ob_flush();
}

// everything on screen is unknown: the next present draws every cell
static void invalidate_front(void) {
    for (size_t i = 0, n = (size_t)gfx_cols * gfx_rows; i < n; i++) front[i].ch = 0xFFFFFFFFu;
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
    for (int y = 0; y < gfx_rows; y++) {
        if (!trow[y]) continue;
        for (int i = 0; i < GFX_NTL; i++) memset(&tl[i][(size_t)y * gfx_cols], 0, (size_t)gfx_cols * sizeof(TCell));
        trow[y] = 0;
    }
}

void text_put(int layer, int x, int y, const char *s, Col c, float a) {
    if (a < 0.02f || y < 0 || y >= gfx_rows) return;
    trow[y] = 1;
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

void text_cell(int layer, int x, int y, uint32_t cp, Col c, float a) {
    if (x < 0 || y < 0 || x >= gfx_cols || y >= gfx_rows || cp <= ' ') return;
    trow[y] = 1;
    TCell *t = &tl[layer][y * gfx_cols + x];
    t->ch = cp; t->c = c; t->a = a;
}

void text_putf(int layer, float xf, int y, const char *s, Col c, float a) {
    int xi = (int)floorf(xf);
    float fr = xf - xi;
    text_put(layer, xi, y, s, c, a * (1 - fr));
    text_put(layer, xi + 1, y, s, c, a * fr);
}

// present
static inline uint8_t to8(float v) {
    int i = (int)(v + 0.5f);
    return (uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

static inline uint8_t q8(float v) { return to8(v) & QMASK; }

// ---- Linux console: 16 palette colors + ordered dithering ----
#define VT_DITHER 18.0f     // dither amplitude, ~ distance between neighboring palette shades

static Col vt_pal[16];
static uint8_t vt_want[16][3], vt_sent[16][3];
static int vt_sent_ok = 0;

void gfx_vt_palette(const Col p[16]) {
    for (int i = 0; i < 16; i++) {
        vt_want[i][0] = to8(p[i].r); vt_want[i][1] = to8(p[i].g); vt_want[i][2] = to8(p[i].b);
        vt_pal[i] = col(vt_want[i][0], vt_want[i][1], vt_want[i][2]);   // what the console really has
    }
}

static void vt_palette_emit(void) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        if (vt_sent_ok && !memcmp(vt_want[i], vt_sent[i], 3)) continue;
        OB_LIT("\x1b]P");       // Linux console: ESC ] P n rrggbb, no terminator
        ob_c(hex[i]);
        for (int k = 0; k < 3; k++) { ob_c(hex[vt_want[i][k] >> 4]); ob_c(hex[vt_want[i][k] & 15]); }
        memcpy(vt_sent[i], vt_want[i], 3);
    }
    vt_sent_ok = 1;
}

static const uint8_t bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static inline float dither(int x, int y) {
    return ((bayer4[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f - 0.5f) * VT_DITHER;
}

static inline float cdist(Col a, Col b) {
    float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return 2 * dr * dr + 4 * dg * dg + 3 * db * db;
}

static int nearest(Col c, int n, float *err) {
    int best = 0;
    float bd = 1e30f;
    for (int i = 0; i < n; i++) {
        float d = cdist(c, vt_pal[i]);
        if (d < bd) { bd = d; best = i; }
    }
    if (err) *err = bd;
    return best;
}

// A cell is: ' ' on bg (0-7), full block in fg (0-15), or upper/lower half with two
// colors. Only 8 backgrounds -> if the "bg" half needs a bright color, flip the half-block.
static void compose_vt(int x, int y, Cell *c, const TCell *out) {
    Col top = gfx_px[(2 * y) * gfx_w + x], bot = gfx_px[(2 * y + 1) * gfx_w + x];
    if (out->ch) {
        Col bg = lerpc(top, bot, 0.5f);
        Col fg = lerpc(bg, out->c, clampf(out->a, 0, 1));
        int bi = nearest(bg, 8, NULL), fi = nearest(fg, 16, NULL);
        if (fi == bi && out->a >= 0.6f) fi = 15;    // no contrast: white
        if (fi != bi) {
            uint32_t cp = vt_map_cp(out->ch);
            c->ch = cp; c->fr = (uint8_t)fi; c->br = (uint8_t)bi;
            return;
        }
        // faint text vanishes into the background: draw the plain cell
    }
    float dt = dither(x, 2 * y), db = dither(x, 2 * y + 1);
    Col T = col(top.r + dt, top.g + dt, top.b + dt), B = col(bot.r + db, bot.g + db, bot.b + db);

    float eT16, eT8, eB16, eB8;
    int t16 = nearest(T, 16, &eT16), t8 = nearest(T, 8, &eT8);
    int b16 = nearest(B, 16, &eB16), b8 = nearest(B, 8, &eB8);

    float best = 1e30f;
    int one = 0;
    for (int j = 0; j < 16; j++) {      // one color for the whole cell
        float e = cdist(T, vt_pal[j]) + cdist(B, vt_pal[j]);
        if (e < best) { best = e; one = j; }
    }
    best -= 1.0f;                       // slight preference to the simple cell
    if (eT16 + eB8 < best) {            // top in fg, bottom in bg
        c->ch = VT_G_UP; c->fr = (uint8_t)t16; c->br = (uint8_t)b8;
        if (t16 == b8) { c->ch = ' '; c->fr = 0; }
        return;
    }
    if (eB16 + eT8 < best) {
        c->ch = VT_G_LO; c->fr = (uint8_t)b16; c->br = (uint8_t)t8;
        if (b16 == t8) { c->ch = ' '; c->fr = 0; }
        return;
    }
    if (one < 8) { c->ch = ' '; c->br = (uint8_t)one; }
    else { c->ch = VT_G_FULL; c->fr = (uint8_t)one; c->br = 0; }
}

static void compose_tc(int x, int y, Cell *c, const TCell *out) {
    Col top = gfx_px[(2 * y) * gfx_w + x], bot = gfx_px[(2 * y + 1) * gfx_w + x];
    if (out->ch) {
        Col bg = lerpc(top, bot, 0.5f);
        Col fg = lerpc(bg, out->c, clampf(out->a, 0, 1));
        c->ch = out->ch;
        c->fr = q8(fg.r); c->fg = q8(fg.g); c->fb = q8(fg.b);
        c->br = q8(bg.r); c->bg = q8(bg.g); c->bb = q8(bg.b);
    } else {
        uint8_t tr = q8(top.r), tg = q8(top.g), tb = q8(top.b);
        uint8_t br = q8(bot.r), bgc = q8(bot.g), bb = q8(bot.b);
        if (tr == br && tg == bgc && tb == bb) {
            c->ch = ' ';
            c->br = tr; c->bg = tg; c->bb = tb;
        } else {
            c->ch = 0x2580;  // half-block
            c->fr = tr; c->fg = tg; c->fb = tb;
            c->br = br; c->bg = bgc; c->bb =bb;
        }
    }
}

// Kitty image mode: the canvas is the image under the cells, so a cell exists only where there is text:
// the glyph in the color it has over the local background; the cell background stays the default,
// the image shows through. No text = a blank cell (and a cell that lost its text is blanked).
static void compose_kgp(int x, int y, Cell *c, const TCell *out) {
    c->ch = ' ';
    if (!out->ch) return;
    Col top = gfx_px[(2 * y) * gfx_w + x], bot = gfx_px[(2 * y + 1) * gfx_w + x];
    Col bg = lerpc(top, bot, 0.5f);
    Col fg = lerpc(bg, out->c, clampf(out->a, 0, 1));
    c->ch = out->ch;
    c->fr = q8(fg.r); c->fg = q8(fg.g); c->fb = q8(fg.b);
}

// Is the cell on screen (a) close enough to the wanted one (b) to not bother the terminal?
// Only plain background cells (' ' or half-block) qualify; glyphs and text are always exact.
static inline int cell_close(const Cell *a, const Cell *b, int tol) {
    if (a->ch != b->ch) return 0;
    if (a->ch != ' ' && a->ch != 0x2580) return 0;
    #define CL(u, v) ((u) > (v) ? (u) - (v) <= tol : (v) - (u) <= tol)
    if (!(CL(a->br, b->br) && CL(a->bg, b->bg) && CL(a->bb, b->bb))) return 0;
    if (a->ch == ' ') return 1;     // foreground is not drawn
    return CL(a->fr, b->fr) && CL(a->fg, b->fg) && CL(a->fb, b->fb);
    #undef CL
}

// ---- fast escape-sequence emitters -------------------------------------------------------
// A changed cell is assembled in a small stack buffer (no per-number buffer checks) and handed
// to the output buffer with one ob_str(). Decimal bytes come from a 256-entry table.
static char    u8s[256][3];
static uint8_t u8n[256];

static void u8_init(void) {
    for (int v = 0; v < 256; v++) {
        char t[3]; int n = 0, x = v;
        do { t[n++] = (char)('0' + x % 10); x /= 10; } while (x);
        for (int i = 0; i < n; i++) u8s[v][i] = t[n - 1 - i];
        u8n[v] = (uint8_t)n;
    }
}

static inline char *put_u8(char *p, unsigned v) {
    memcpy(p, u8s[v], 3);           // always copy 3 (buffer has slack), advance by the real length
    return p + u8n[v];
}

static inline char *put_uint(char *p, unsigned v) {
    if (v < 256) return put_u8(p, v);
    char t[10]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    return p;
}

static inline char *put_rgb(char *p, int is_bg, const uint8_t *c) {
    memcpy(p, is_bg ? "48;2;" : "38;2;", 5); p += 5;
    p = put_u8(p, c[0]); *p++ = ';';
    p = put_u8(p, c[1]); *p++ = ';';
    return put_u8(p, c[2]);
}

void gfx_present(void) {
    const int vt = vt_active;
    int kg = !vt && kgp_active;
    if (kg && !kgp_frame(gfx_px, gfx_w, gfx_h, gfx_cols, gfx_rows)) {   // no shared memory after all: the backend is off for good,
        kg = 0;                                     // draw this frame (and the rest) with half-blocks
        invalidate_front();
    }

    // 1. canvas + text -> back[]
    for (int y = 0; y < gfx_rows; y++)
        for (int x = 0; x < gfx_cols; x++) {
            int i = y * gfx_cols + x;
            Cell c;
            memset(&c, 0, sizeof c);

            TCell out;
            memset(&out, 0, sizeof out);
            for (int L = 0; trow[y] && L < GFX_NTL; L++) {
                const TCell *t = &tl[L][i];
                if (!t->ch) continue;
                if (out.ch) { out.c = lerpc(out.c, t->c, t->a); out.a = 1 - (1 - out.a) * (1 - t->a); }
                else out = *t;
                out.ch = t-> ch;
            }

            if (vt) compose_vt(x, y, &c, &out);
            else if (kg) compose_kgp(x, y, &c, &out);
            else compose_tc(x, y, &c, &out);
            back[i] = c;
        }

    // 2. diff with front[] -> escape-sequences
    ob_reset();
    if (vt) vt_palette_emit();
    else OB_LIT("\x1b[?2026h");   // synchronized update: without frame rips
    if (kg) kgp_emit(gfx_cols, gfx_rows);
    else kgp_emit_clear();          // only does something right after the backend gave up
    if (!u8n[0]) u8_init();
    const int tol = (vt || kg) ? 0 : gfx_tol;
    int cx = -1, cy = -1;
    int fgv = 0, bgv = 0;
    uint8_t cfr = 0, cfg = 0, cfb = 0, cbr = 0, cbg = 0, cbb = 0;
    for (int y = 0; y < gfx_rows; y++)
        for (int x = 0; x < gfx_cols; x++) {
            int i = y * gfx_cols + x;
            if (!memcmp(&back[i], &front[i], sizeof(Cell))) continue;
            if (tol && cell_close(&front[i], &back[i], tol)) {
                back[i] = front[i];     // keep what is really on screen: the error never accumulates past tol
                continue;
            }
            const Cell *c = &back[i];
            char sb[112], *p = sb;
            if (cy != y || cx != x) {
                *p++ = '\x1b'; *p++ = '[';
                if (cy == y && x > cx) {                    // same row, to the right: CUF is shorter than CUP
                    p = put_uint(p, (unsigned)(x - cx));
                    *p++ = 'C';
                } else {
                    p = put_uint(p, (unsigned)y + 1); *p++ = ';';
                    p = put_uint(p, (unsigned)x + 1); *p++ = 'H';
                }
            }
            if (vt) {
                // console: fg 0-15 (bright = bold), bg 0-7
                int nf = c->ch != ' ' && (!fgv || cfr != c->fr);
                int nb = !bgv || cbr != c->br;
                if (nf || nb) {
                    *p++ = '\x1b'; *p++ = '[';
                    if (nf) {
                        if (c->fr >= 8) { *p++ = '1'; *p++ = ';'; } else { *p++ = '2'; *p++ = '2'; *p++ = ';'; }
                        p = put_uint(p, 30u + (c->fr & 7u));
                        cfr = c->fr; fgv = 1;
                    }
                    if (nf && nb) *p++ = ';';
                    if (nb) { p = put_uint(p, 40u + c->br); cbr = c->br; bgv = 1; }
                    *p++ = 'm';
                }
            } else if (kg) {
                // text over the image: only the foreground, the background stays the default
                int nf = c->ch != ' ' && (!fgv || cfr != c->fr || cfg != c->fg || cfb != c->fb);
                if (nf) {
                    *p++ = '\x1b'; *p++ = '[';
                    p = put_rgb(p, 0, &c->fr); cfr = c->fr; cfg = c->fg; cfb = c->fb; fgv = 1;
                    *p++ = 'm';
                }
            } else {
                int nf = c->ch != ' ' && (!fgv || cfr != c->fr || cfg != c->fg || cfb != c->fb);
                int nb = !bgv || cbr != c->br || cbg != c->bg || cbb != c->bb;
                if (nf || nb) {                             // one SGR for both colors
                    *p++ = '\x1b'; *p++ = '[';
                    if (nf) { p = put_rgb(p, 0, &c->fr); cfr = c->fr; cfg = c->fg; cfb = c->fb; fgv = 1; }
                    if (nf && nb) *p++ = ';';
                    if (nb) { p = put_rgb(p, 1, &c->br); cbr = c->br; cbg = c->bg; cbb = c->bb; bgv = 1; }
                    *p++ = 'm';
                }
            }
            if (c->ch == ' ') *p++ = ' ';
            else if (c->ch == 0x2580) { *p++ = (char)0xE2; *p++ = (char)0x96; *p++ = (char)0x80; }
            else { ob_str(sb, (size_t)(p - sb)); ob_cp(c->ch); cx = x + 1; cy = y; continue; }
            ob_str(sb, (size_t)(p - sb));
            cx = x + 1;
            cy = y;
        }
    if (!vt) OB_LIT("\x1b[?2026l");
    memcpy(front, back, (size_t)gfx_cols * gfx_rows * sizeof(Cell));
    ob_flush();
}
