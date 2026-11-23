#include "world.h"

#include <math.h>
#include <string.h>

static const float VENT_FX[3] = {0.14f, 0.50f, 0.83f};   // bottom sources' positions (width proportion)

void world_init(World *w, int pw, int ph) {
    memset(w, 0, sizeof *w);
    w->w = pw;
    w->h = ph;
    w->vent_t[0] = 0.5f; w->vent_t[1] = 1.2f; w->vent_t[2] = 2.0f;
    for (int i = 0; i < 3; i++) world_add_fish(w);
    w->nweed = 14;
    for (int i = 0; i < w->nweed; i++) weed_init(&w->weed[i], i, w->nweed);
}

void world_resize(World *w, int pw, int ph) {
    w->w = pw;
    w->h = ph;
    for (int i = 0; i < w->nfish; i++) {
        Fish *f = &w->fish[i];
        float m = SPR_W * 0.5f + 1;
        f->x = clampf(f->x, m, fmaxf(m, pw - m));
        f->y = clampf(f->y, 3, fmaxf(3, world_fish_ymax(w)));
        f->px = f->x; f->py = f->y;
        f->tt = 0;   // вchoose new target
    }
    for (int i = 0; i < MAX_BUB; i++)
        if (w->bub[i].on && (w->bub[i].x0 >= pw || w->bub[i].y >= ph)) w->bub[i].on = 0;
    for (int i = 0; i < MAX_FOOD; i++)
        if (w->food[i].on && (w->food[i].x >= pw || w->food[i].y >= ph)) w->food[i].on = 0;
}

void world_update(World *w, float dt) {
    w->t += dt;
    for (int i = 0; i < 3; i++) {
        w->vent_t[i] -= dt;
        if (w->vent_t[i] <= 0) {
            w->vent_t[i] = rndr(0.4f, 2.2f);
            world_spawn_bubble(w, w->w * VENT_FX[i] + rndr(-1, 1), (float)(w->h - 5), rndr(0.9f, 2.0f));
        }
    }
    for (int i = 0; i < w->nfish; i++) fish_update(w, &w->fish[i], dt);
    for (int i = 0; i < MAX_BUB; i++) bubble_update(&w->bub[i], dt);
    for (int i = 0; i < MAX_FOOD; i++) food_update(w, &w->food[i], dt);
}

void world_spawn_bubble(World *w, float x, float y, float r) {
    for (int i = 0; i < MAX_BUB; i++)
        if (!w->bub[i].on) { bubble_init(&w->bub[i], x, y, r); return; }   // pool is full - skip quietly
}

void world_bubble_burst(World *w, int n) {
    for (int k = 0; k < n; k++)
        world_spawn_bubble(w, rndr(2, (float)w->w - 2), (float)(w->h - 5), rndr(0.9f, 2.4f));
}

void world_drop_food(World *w, float x, float y) {
    for (int i = 0; i < MAX_FOOD; i++)
        if (!w->food[i].on) { food_init(&w->food[i], x, y); return; }
}

void world_drop_food_burst(World *w, float x, float y) {
    for (int i = 0; i < 3; i++) world_drop_food(w, x + rndr(-2, 2), y + rndr(-1, 1));
}

void world_add_fish(World *w) {
    if (w->nfish >= MAX_FISH) return;
    fish_init(&w->fish[w->nfish], w, w->nfish);
    w->nfish++;
}