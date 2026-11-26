#include "sprite.h"

#include <math.h>

#include "gfx.h"

// Sprite variants. They differ in looks only: every one is SPR_W x SPR_H, faces left, and has its tail in
// the columns >= 8 (that is where it wags), because the hitbox, the walls, the mouth bubble and the name
// label are all measured from this footprint. Keep that when adding one.
//   B body   T tail (lighter)   D fin / stripes (darker)   W eye shine   E eye   . empty
static const char SPRITES[SPR_KINDS][SPR_H][SPR_W + 1] = {   // rows: >= SPR_W+2 chars is a compiler warning, a short row is padded with empty pixels
    {   // 0: classic
        "....DDD.......",
        "..BBBBBBB...T.",
        ".BBBWBBBBB.TT.",
        "BBBBEBBBBBTTTT",
        ".BBBBBBBBB.TT.",
        "..BBBBBBB...T.",
        "....DDD.......",
    },
    {   // 1: angelfish - triangular body: pointed nose, tall straight back, one dark bar
        ".....DDDDD....",
        "....BBBB....TT",
        "..BBWBBBB..TT.",
        "BBBBEBBBBBTT..",
        "..BBBBBBB..TT.",
        "....BBBB....TT",
        ".....DDDDD....",
    },
    {   // 2: round - big round body, light belly, short tail
        "...BBBBBB.....",
        "..BBBBBBBBB.T.",
        ".BBBWBBBBBBBTT",
        "BBBBEBBBBBBBTT",
        ".BBTTTTTTTBBTT",
        "..BBTTTTTBB.T.",
        "...BBBBB......",
    },
    {   // 3: slender - long thin body, light belly, small fins
        "......DDD.....",
        ".BBBBBBBBB..T.",
        "BBWBBBBBBBTTT.",
        "BBEBBBBBBBTTTT",
        ".BTTTTTTTBTTT.",
        ".BBBBBBBBB..T.",
        "........DD....",
    },
    {   // 4: striped - classic shape with two dark bars
        "...DDDD.......",
        "..BBBDBBD...T.",
        ".BBWBDBBDB.TT.",
        "BBBEBDBBDBTTTT",
        ".BBBBDBBDB.TT.",
        "..BBBDBBD...T.",
        "....DDD.......",
    },
};

static int tex(int kind, int sx, int sy, const FishPalette *p, Col *out) {
    if (sx < 0 || sy < 0 || sx >= SPR_W || sy >= SPR_H) return 0;
    switch (SPRITES[kind][sy][sx]) {
        case 'B': *out = p->body; return 1;
        case 'T': *out = p->tail; return 1;
        case 'D': *out = p->fin; return 1;
        case 'W': *out = col(240, 240, 235); return 1;
        case 'E': *out = col(10, 10, 20); return 1; 
    }
    return 0;
}

void sprite_draw_fish(int kind, const FishPalette *pal, float cx, float cy, float face, float phase,
                       float roll, float opacity) {
    if (kind < 0 || kind >= SPR_KINDS) kind = 0;
    float scale = fmaxf(fabsf(face), 0.12f);
    float rscale = fmaxf(fabsf(roll), 0.12f);
    int x0 = (int)floorf(cx - SPR_W / 2 - 2), x1 = (int)ceilf(cx + SPR_W / 2 + 2);
    int y0 = (int)floorf(cy - SPR_H / 2 - 3), y1 = (int)ceilf(cy + SPR_H / 2 + 3);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > gfx_w - 1) x1 = gfx_w - 1;
    if (y1 > gfx_h - 1) y1 = gfx_h - 1;
    float wig = sinf(phase);

    for (int iy = y0; iy <= y1; iy++) {
        for (int ix = x0; ix <= x1; ix++) {
            float u = (ix + 0.5f - cx) / scale;
            float v = (iy + 0.5f - cy) / rscale;
            if (roll < 0) v = -v;
            if (face > 0) u = -u;
            float sx = u + SPR_W * 0.5f;
            float tf = clampf((sx - 8.0f) / 5.0f, 0, 1);
            float sy = v + SPR_H * 0.5f - wig * 0.9f * tf * tf;
            float fx = sx - 0.5f, fy = sy - 0.5f;
            int tx = (int)floorf(fx), ty = (int)floorf(fy);
            float ax = fx - tx, ay = fy - ty;
            float ar = 0, ag = 0, ab = 0, aa = 0;
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    Col c;
                    if (tex(kind, tx + i, ty + j, pal, &c)) {
                        float w = (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
                        ar += c.r * w; ag += c.g * w; ab += c.b * w; aa += w;
                    }
                }
            if (aa < 0.02f) continue;
            Col c = col(ar / aa, ag / aa, ab / aa);
            float alpha = clampf((aa - 0.5f) * 1.8f + 0.5f, 0, 1);
            gfx_blend(ix, iy, c, alpha * opacity);
        }
    }
}