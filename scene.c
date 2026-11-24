#include "scene.h"

#include <math.h>

#include "gfx.h"
#include "sprite.h"
#include "theme.h"

static void draw_background(float t, const Theme *th) {
    float hy = fmaxf(8.0f, th->horizon * (float)(gfx_h - 1));   // where the gradient reaches the bottom color
    for (int y = 0; y < gfx_h; y++) {
        float v = clampf((float)y / hy, 0, 1);
        Col base = lerpc(th->water_top, th->water_bot, powf(v, th->grad_pow));
        float fall = (1 - v);
        fall = fall * fall;
        for (int x = 0; x < gfx_w; x++) {
            // two sets of light rays swimming to different sides
            float r1 = sinf(x * 0.09f + t * 0.35f + y * 0.03f) * 0.5f + 0.5f;
            float r2 = sinf(x * 0.043f - t * 0.20f + y * 0.02f) * 0.5f + 0.5f;
            r1 *= r1; r1 *= r1;
            r2 *= r2; r2 *= r2;
            float k = (r1 * 0.6f + r2 * 0.4f) * 0.55f * fall;
            Col c = col(base.r + k * th->ray.r, base.g + k * th->ray.g, base.b + k * th->ray.b);
            // water surface
            float surf = 1.5f + sinf(x * 0.2f + t * 2.0f) * 0.5f;
            if (y < surf) c = lerpc(c, th->surface, clampf(surf - y, 0, 1) * 0.5f);
            gfx_px[y * gfx_w + x] = c;
        }
    }
}

// retrowave sun: gradient disc cut by the horizon, black stripes grow towards the bottom
static void draw_sun(const Theme *th) {
    if (th->sun < 0.01f) return;
    float hy = th->horizon * (float)(gfx_h - 1);
    float R = fminf(gfx_w * 0.16f, hy * 0.5f);
    if (R < 3.0f) return;
    float cx = gfx_w * 0.5f, cy = hy - R * 0.45f;
    float Rg = R * 2.6f;    // glow radius: it fades to exactly 0 here, so the box we loop over is invisible
    int y0 = (int)floorf(cy - Rg), y1 = (int)ceilf(hy);
    int x0 = (int)floorf(cx - Rg), x1 = (int)ceilf(cx + Rg);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float v = clampf((y + 0.5f - (cy - R)) / (2 * R), 0, 1);
            float win = clampf(1.0f - d / Rg, 0, 1);
            win = win * win * (3.0f - 2.0f * win);              // smoothstep window
            float glow = expf(-(d * d) / (R * R * 2.2f)) * 0.30f * win;
            gfx_blend(x, y, th->sun_b, glow * th->sun);
            float cover = clampf(R - d + 0.5f, 0, 1);
            if (cover <= 0) continue;
            float cut = 0;
            if (v > 0.5f) {
                float s = (v - 0.5f) * 2.0f;
                float ph = fmodf(s * 4.5f, 1.0f);
                cut = clampf((s * 0.8f - ph) * 3.0f + 0.5f, 0, 1);
            }
            gfx_blend(x, y, lerpc(th->sun_a, th->sun_b, v), cover * (1 - cut) * th->sun);
        }
}

// neon grid floor in perspective; lines run towards the viewer
static void draw_grid(float t, const Theme *th) {
    if (th->grid < 0.01f) return;
    int y0 = (int)ceilf(th->horizon * (float)(gfx_h - 1));
    if (y0 >= gfx_h) return;
    float span = (float)(gfx_h - y0), cx = gfx_w * 0.5f;
    for (int y = y0; y < gfx_h; y++) {
        float d = ((float)y + 0.5f - y0) / span;                    // 0 at the horizon, 1 at the bottom
        float z = 1.0f / (d * 0.92f + 0.08f);
        float ph = z * 0.55f - t * 0.6f;
        float fh = ph - floorf(ph);
        float px_h = fminf(fh, 1 - fh) / (0.55f * 0.92f * z * z / span);    // distance to the line in pixels
        float cov_h = clampf(1.0f - px_h / 1.1f, 0, 1);
        float fade = clampf(d * 2.5f, 0, 1);
        float sc = gfx_w * 0.5f * (0.03f + d);                      // screen px per unit of world X
        for (int x = 0; x < gfx_w; x++) {
            float fx = ((x + 0.5f - cx) / sc) / 0.22f;           // vertical lines every 0.22 of the world X
            fx -= floorf(fx);
            float cov_v = clampf(1.0f - fminf(fx, 1 - fx) * sc * 0.22f / 1.5f, 0, 1);
            float cov = fmaxf(cov_h, cov_v) * fade;
            Col c = lerpc(th->grid_floor, th->grid_line, cov);
            c = lerpc(c, th->water_bot, powf(1 - d, 6.0f) * 0.35f);  // glow at the horizon
            gfx_blend(x, y, c, th->grid);
        }
    }
}

static void draw_sand(const Theme *th) {
    float a = 1.0f - th->grid;      // grid floor replaces sand
    if (a < 0.01f) return;
    for (int x = 0; x < gfx_w; x++) {
        uint32_t h = hash32((uint32_t)x * 2654435761u + 7u);
        int sh = 3 + (int)(h & 1);
        for (int y = gfx_h - sh; y < gfx_h; y++) {
            float shade = 0.75f + (float)((h >> 8) & 15) / 60.0f;
            float depth = (float)(y - (gfx_h - sh)) / (float)sh;
            float d = 1.0f - depth * 0.4f;
            gfx_blend(x, y, scalec(th->sand, shade * d), a);
        }
    }
}

static void draw_weeds(const World *w, const Theme *th, int front_pass) {
    float floor_y = world_floor_y(w);
    for (int i = 0; i < w->nweed; i++) {
        const Weed *wd = &w->weed[i];
        if (wd->front != front_pass) continue;
        Col wc = scalec(wd->front ? th->weed_front : th->weed_back, wd->g);
        float bx = wd->fx * gfx_w;
        float h = fmaxf(6.0f, wd->hf * gfx_h);
        for (float yy = floor_y; yy > floor_y - h; yy -= 1.0f) {
            float k = (floor_y - yy) / h;   // 0 at the root -> 1 at the top
            float sway = sinf(w->t * 1.3f + wd->ph +k * 3.2f) * wd->amp * k;
            Col c = lerpc(wc, col(wc.r * 1.6f, wc.g * 1.4f, wc.b * 1.4f), k);
            gfx_splat(bx + sway, yy, c, 0.9f);
            gfx_splat(bx + sway + 1.0f, yy, c, 0.9f);
        }
    }
}

static void draw_bubble(float x, float y, float r, float a, Col bc) {
    int x0 = (int)floorf(x - r - 1.5f), x1 = (int)ceilf(x + r + 1.5f);
    int y0 = (int)floorf(y - r - 1.5f), y1 = (int)ceilf(y + r + 1.5f);
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

// palette of a fish for the current theme; dying ones fade to gray, roll over and become transparent
static void fish_look(const Theme *th, const Fish *f, FishPalette *p, float *roll, float *opac) {
    Col b = th->fish[f->ci % THEME_FISH];
    *roll = 1.0f;
    *opac = 1.0f;
    if (f->dying) {
        float t = f->dtime;
        float l = 0.30f * b.r + 0.59f * b.g + 0.11f * b.b;
        b = lerpc(b, col(l * 0.8f + 20, l * 0.8f + 20, l * 0.8f + 24), clampf(t / 1.0f, 0, 1));
        *roll = cosf(3.14159265f * clampf(t / 0.7f, 0, 1));
        *opac = clampf((FISH_DIE_T - t) / 0.7f, 0, 1);
    }
    p->body = b;
    p->tail = lerpc(b, col(255, 255, 255), 0.35f);
    p->fin = scalec(b, 0.6f);
}

void scene_draw(const World *w, float alpha) {
    const Theme *th = theme_cur();
    draw_background(w->t, th);
    draw_sun(th);
    draw_grid(w->t, th);
    draw_sand(th);
    draw_weeds(w, th, 0);

    for (int i = 0; i < w->nfish; i++) {
        const Fish *f = &w->fish[i];
        FishPalette pal;
        float roll, opac;
        fish_look(th, f, &pal, &roll, &opac);
        sprite_draw_fish(&pal, f->px + (f->x - f->px) * alpha, f->py + (f->y - f->py) * alpha,
            f->face, f->phase, roll, opac);
    }
    for (int i = 0; i < MAX_FOOD; i++)
        if (w->food[i].on) gfx_splat(w->food[i].x, w->food[i].y, th->food, 1.0f);
    for (int i = 0; i < MAX_BUB; i++) {
        const Bubble *b = &w->bub[i];
        if (!b->on) continue;
        float a = b->pop > 0 ? b->pop / POP_T : 1.0f;
        draw_bubble(b->px + (b->x - b->px) * alpha, b->py + (b->y - b->py) * alpha, b->r, a, th->bubble);
    }
    draw_weeds(w, th, 1);   // upper layer
}
