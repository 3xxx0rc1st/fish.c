#include "scene.h"

#include <math.h>

#include "gfx.h"
#include "sprite.h"

static void draw_background(float t) {
    Col top = col(30, 130, 165), bot = col(4, 16, 44);
    for (int y = 0; y < gfx_h; y++) {
        float v = (float)y / (float)(gfx_h - 1);
        Col base = lerpc(top, bot, powf(v, 0.7f));
        float fall = (1 - v);
        fall = fall * fall;
        for (int x = 0; x < gfx_w; x++) {
            // two sets of light rays swimming to different sides
            float r1 = sinf(x * 0.09f + t * 0.35f + y * 0.03f) * 0.5f + 0.5f;
            float r2 = sinf(x * 0.043f - t * 0.20f + y * 0.02f) * 0.5f + 0.5f;
            r1 *= r1; r1 *= r1;
            r2 *= r2; r2 *= r2;
            float k = (r1 * 0.6f + r2 * 0.4f) * 0.55f * fall;
            Col c = col(base.r + k * 80, base.g + k * 100, base.b + k * 120);
            // water surface
            float surf = 1.5f + sinf(x * 0.2f + t * 2.0f) * 0.5f;
            if (y < surf) c = lerpc(c, col(170, 230, 240), clampf(surf - y, 0, 1) * 0.5f);
            gfx_px[y * gfx_w + x] = c;
        }
    }
}

static void draw_sand(void) {
    for (int x = 0; x < gfx_w; x++) {
        uint32_t h = hash32((uint32_t)x * 2654435761u + 7u);
        int sh = 3 + (int)(h & 1);
        for (int y = gfx_h - sh; y < gfx_h; y++) {
            float shade = 0.75f + (float)((h >> 8) & 15) / 60.0f;
            float depth = (float)(y - (gfx_h - sh)) / (float)sh;
            float d = 1.0f - depth * 0.4f;
            gfx_px[y * gfx_w + x] = col(150 * shade * d, 125 * shade * d, 85 * shade * d);
        }
    }
}

static void draw_weeds(const World *w, int front_pass) {
    float floor_y = world_floor_y(w);
    for (int i = 0; i < w->nweed; i++) {
        const Weed *wd = &w->weed[i];
        if (wd->front != front_pass) continue;
        float bx = wd->fx * gfx_w;
        float h = fmaxf(6.0f, wd->hf * gfx_h);
        for (float yy = floor_y; yy > floor_y - h; yy -= 1.0f) {
            float k = (floor_y - yy) / h;   // 0 at the root -> 1 at the top
            float sway = sinf(w->t * 1.3f + wd->ph +k * 3.2f) * wd->amp * k;
            Col c = lerpc(wd->c, col(wd->c.r * 1.6f, wd->c.g * 1.4f, wd->c.b * 1.4f), k);
            gfx_splat(bx + sway, yy, c, 0.9f);
            gfx_splat(bx + sway + 1.0f, yy, c, 0.9f);
        }
    }
}

static void draw_bubble(float x, float y, float r, float a) {
    int x0 = (int)floorf(x - r - 1.5f), x1 = (int)ceilf(x + r + 1.5f);
    int y0 = (int)floorf(y - r - 1.5f), y1 = (int)ceilf(y + r + 1.5f);
    Col bc = col(190, 235, 255);
    for (int iy = y0; iy <= y1; iy++)
        for (int ix = x0; ix <= x1; ix++) {
            float dx = ix + 0.5f - x, dy = iy + 0.5f - y;
            float d = sqrtf(dx * dx + dy * dy);
            float ring = clampf(1.0f - fabsf(d - r) / 0.9f, 0, 1);  // antialiased contour
            float fill = d < r ? 0.10f : 0.0f;
            gfx_blend(ix, iy, bc, (ring * 0.65f + fill) * a);
            if (r >= 1.4f) {    // glare
                float hx = ix + 0.5f - (x - r * 0.4f), hy = iy + 0.5f - (y - r * 0.4f);
                float hl = clampf(1.0f - sqrtf(hx * hx + hy * hy) / 0.9f, 0, 1);
                gfx_blend(ix, iy, col(255, 255, 255), hl * 0.8f * a);
            }
        }
}

void scene_draw(const World *w, float alpha) {
    draw_background(w->t);
    draw_sand();
    draw_weeds(w, 0);

    for (int i = 0; i < w->nfish; i++) {
        const Fish *f = &w->fish[i];
        sprite_draw_fish(&f->pal, f->px + (f->x - f->px) * alpha, f->py + (f->y - f->py) * alpha,
            f->face, f->phase);
    }
    for (int i = 0; i < MAX_FOOD; i++)
        if (w->food[i].on) gfx_splat(w->food[i].x, w->food[i].y, col(230, 170, 80), 1.0f);
    for (int i = 0; i < MAX_BUB; i++) {
        const Bubble *b = &w->bub[i];
        if (!b->on) continue;
        float a = b->pop > 0 ? b->pop / POP_T : 1.0f;
        draw_bubble(b->px + (b->x - b->px) * alpha, b->py + (b->y - b->py) * alpha, b->r, a);
    }
    draw_weeds(w, 1);   // upper layer
}