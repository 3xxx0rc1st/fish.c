// gfx - subpixel canvas (half-block), text layers, diff-render in terminal
//
// Canvas: width = cols, height = rows*2.
// Pixel (x, y) covers [x, x+1) X [y, y+1]; all coords - float;
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#include "util.h"

#define GFX_NTL 3   // text layers: 0 - bottom, 2 - top

extern int gfx_cols, gfx_rows;  // size in cells
extern int gfx_w, gfx_h;        // size in pixels (gfx_h = gfx_rows * 2)
extern Col *gfx_px;             // canvas, gfx_w X gfx_h

void gfx_resize(int cols, int rows);    // recreates buffers, forces full redraw
void gfx_present(void);                 // canvas + text -> cells -> diff -> one write()

// mix c color with pixel with multiplier a
static inline void gfx_blend(int x, int y, Col c, float a) {
    if (x < 0 || y < 0 || x >= gfx_w || y >= gfx_h || a <= 0.0f) return;
    Col *p = &gfx_px[y * gfx_w + x];
    p->r += (c.r - p->r) * a;
    p->g += (c.g - p->g) * a;
    p->b += (c.b - p->b) * a;
}

// Linux console mode (vt_active): the console has 16 colors, not truecolor.
// pal - what is loaded into its palette now; gfx_present sends changes itself.
// Indices 0-7 may be backgrounds, 8-15 foreground only (see theme_vt_palette).
void gfx_vt_palette(const Col pal[16]);

// bilinear "splat" of a dot in float coords - more smoothness
void gfx_splat(float x, float y, Col c, float a);

// text
uint32_t utf8_next(const char **p);     // next codepoint, p shifts
int utf8_len(const char *s);            // length in codepoints

void text_clear(void);
// overlay and opacity
void text_put(int layer, int x, int y, const char *s, Col c, float a);
// one codepoint into a cell, replaces what is there (used by the shell panel)
void text_cell(int layer, int x, int y, uint32_t cp, Col c, float a);
// float X: two neighboring cells with crossfade - text "floats" without rips
void text_putf(int layer, float xf, int y, const char *s, Col c, float a);

#endif
