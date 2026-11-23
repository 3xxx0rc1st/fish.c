#include "entity.h"

#include <math.h>
#include <string.h>

#include "world.h"

static const Col P_BODY[] = {
    {255, 140, 40}, {80, 160, 255}, {255, 100, 160}, {110, 220, 110}, {240, 220, 80}, {170, 120, 255},
};
static const char *NAMES[] = {"Bob", "Karl", "Yuri", "Nemo", "Kusya", "Bublik", "Chub", "Glub"};

void fish_init(Fish *f, const World *w, int index) {
    memset(f, 0, sizeof *f);
    f->x = f->px = rndr(SPR_W, fmaxf(SPR_W + 1, (float)w->w - SPR_W));
    f->y = f->py = rndr(4, fmaxf(5, world_fish_ymax(w)));
    f->dir = rnd() < 0.5f ? -1 : 1;
    f->face = (float)f->dir;
    f->spd = rndr(11, 18);
    f->bt = rndr(1, 4);
    f->pal.body = P_BODY[index % 6];
    f->pal.tail = lerpc(f->pal.body, col(255, 255, 255), 0.35f);
    f->pal.fin = col(f->pal.body.r * 0.6f, f->pal.body.g * 0.6f, f->pal.body.b * 0.6f);
    f->name = NAMES[index % 8];
}

void fish_update(World *w, Fish *f, float dt) {
    f->px = f->x; f->py = f->y;
    const float ymax = world_fish_ymax(w);

    // target: nearest food or wandering
    f->hunting = 0;
    float best = 1e9f;
    for (int i = 0; i < MAX_FOOD; i++) {
        const Food *fd = &w ->food[i];
        if (!fd->on) continue;
        float d = hypotf(fd->x - f->x, fd->y - f->y);
        if (d < best) { best = d; f->tx = fd->x; f->ty = fd->y; f->hunting = 1; }
    }
    float dx = f->tx - f->x, dy = f->ty - f->y;
    float dist = hypotf(dx, dy);
    if (!f->hunting) {
        f->tt -= dt;
        if (f->tt <= 0 || dist < 3) {
            float m = SPR_W * 0.5f + 2;
            f->tx = rndr(m, fmaxf(m + 1, (float)w->w - m));
            f->ty = rndr(4, fmaxf(5, ymax));
            f->tt = rndr(3, 8);
        }
    }
    dx = f->tx - f->x; dy = f->ty - f->y;
    dist = fmaxf(hypotf(dx, dy), 0.001f);

    // steering
    float sp = f->hunting ? 26.0f : f-> spd;
    if (!f->hunting) sp *= clampf(dist / 12.0f, 0.35f, 1.0f);
    float k = clampf((f->hunting ? 3.0f : 1.4f) * dt, 0, 1);
    f->vx += (dx / dist * sp - f->vx) * k;
    f->vy += (dy / dist * sp * 0.6f - f->vy) * k;
    f->x += f->vx * dt;
    f->y += f->vy * dt;

    float m = SPR_W * 0.5f + 1;
    if (f->x < m) { f->x = m; f->vx = fabsf(f->vx); }
    if (f->x > w->w - m) { f->x = w->w - m; f->vx = -fabsf(f->vx); }
    if (f->y < 3) { f->y = 3; f->vy = fabsf(f->vy); }
    if (f->y > ymax) { f->y = ymax; f->vy = -fabsf(f->vy); }

    // turn
    if (f->vx > 2.0f) f-> dir = 1;
    else if (f->vx < -2.0f) f->dir = -1;
    f->face += clampf((float)f->dir - f->face, -5.0f * dt, 5.0f * dt);
    f->phase += dt * (5.0 + hypotf(f->vx, f->vy) * 0.5f);

    float mx = f->x + (f->dir < 0 ? -1 : 1) * (SPR_W * 0.5f - 1);
    f->bt -= dt;
    if (f->bt <= 0) {
        f->bt = rndr(1.5f, 5);
        world_spawn_bubble(w, mx, f->y, rndr(0.9f, 1.8f));
    }

    // eating
    for (int i = 0; i < MAX_FOOD; i++) {
        if (!w->food[i].on) continue;
        if (hypotf(w->food[i].x - mx, w->food[i].y - f->y) < 3.5f) {
            w->food[i].on = 0;
            for (int j = 0; j < 4; j++)
                world_spawn_bubble(w, mx + rndr(-1, 1), f->y + rndr(-1, 1), rndr(0.9f, 1.6f));
            break;
        }
    }
}

// bubbles
void bubble_init(Bubble *b, float x, float y, float r) {
    memset(b, 0, sizeof *b);
    b->on = 1;
    b->x0 = b->x = b->px = x;
    b->y = b->py = y;
    b->r = r;
    b->vy = -(6.0 + r * 4.0f + rndr(0, 4)); // bigger float faster
    b->ph = rndr(0, 6.28f);
    b->fr = rndr(1.5f, 3.5f);
    b->amp = rndr(0.3f, 1.6f);
    b->gr = rndr(0.02f, 0.12f);
}

void bubble_update(Bubble *b, float dt) {
    if (!b->on) return;
    b->px = b->x; b->py = b->y;
    if (b->pop > 0) {
        b->pop -= dt;
        b->r += dt * 8.0f;
        if (b->pop <= 0) b->on = 0;
        return;
    }
    b->y += b->vy * dt;
    b->ph += b->fr * dt;
    b->x = b->x0 + sinf(b->ph) * b->amp;
    b->r += b->gr * dt;
    if (b->y - b->r < 1.0f) b->pop = POP_T; // pops when gets to surface
}

// food
void food_init(Food *f, float x, float y) {
    *f = (Food){x, y, rndr(5, 8), 0, 1};
}

void food_update(const World *w, Food *f, float dt) {
    if (!f->on) return;
    f->age += dt;
    if (f->y < (float)(w->h - 5)) {
        f->y += f->vy * dt;
        f->x += sinf(f->age * 3.0f + f->x) * 2.0f * dt;
    }
    if (f->age > 30.0f) f->on = 0;
}

// seaweed
void weed_init(Weed *wd, int i, int n) {
    wd->fx = (i + rndr(0.1f, 0.9f)) / (float)n;
    wd->hf = rndr(0.12f, 0.30f);
    wd->ph = rndr(0, 6.28f);
    wd->amp = rndr(1.5f, 3.5f);
    wd->front = (i % 4 == 0);
    float g = rndr(0.7f, 1.0f);
    wd->c = wd->front ? col(20 * g, 90 * g, 50 * g) : col(35 * g, 150 * g, 80 * g);
}