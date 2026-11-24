#include "entity.h"

#include <math.h>
#include <string.h>

#include "theme.h"
#include "world.h"

static const char *NAMES[] = {"Bob", "Karl", "Yuri", "Nemo", "Kusya", "Bublik", "Chub", "Glub"};
#define NNAMES ((int)(sizeof NAMES / sizeof *NAMES))

// first name that none of the living fish has
static const char *pick_name(const World *w) {
    for (int k = 0; k < NNAMES; k++) {
        int used = 0;
        for (int i = 0; i < w->nfish; i++)
            if (!w->fish[i].dying && w->fish[i].name == NAMES[k]) used = 1;
        if (!used) return NAMES[k];
    }
    return NAMES[0];
}

void fish_init(Fish *f, const World *w) {
    memset(f, 0, sizeof *f);
    f->x = f->px = rndr(SPR_W, fmaxf(SPR_W + 1, (float)w->w - SPR_W));
    f->y = f->py = rndr(4, fmaxf(5, world_fish_ymax(w)));
    f->dir = rnd() < 0.5f ? -1 : 1;
    f->face = (float)f->dir;
    f->spd = rndr(11, 18);
    f->bt = rndr(1, 4);
    f->ci = w->spawned % THEME_FISH;
    f->name = pick_name(w);
}

void fish_kill(World *w, Fish *f) {
    if (f->dying) return;
    f->dying = 1;
    f->dtime = 0;
    f->vx *= 0.3f;
    f->vy = 0;
    for (int j = 0; j < 3; j++)
        world_spawn_bubble(w, f->x + rndr(-2, 2), f->y + rndr(-1, 1), rndr(0.9f, 1.6f));
}

// dead fish: no hunting, drifts up belly-up, then pops into bubbles
static void fish_update_dying(World *w, Fish *f, float dt) {
    f->px = f->x; f->py = f->y;
    f->dtime += dt;
    f->vx -= f->vx * clampf(2.0f * dt, 0, 1);
    f->vy += (-3.5f - f->vy) * clampf(1.5f * dt, 0, 1);
    f->x += f->vx * dt;
    f->y += f->vy * dt;
    float m = SPR_W * 0.5f + 1;
    f->x = clampf(f->x, m, fmaxf(m, w->w - m));
    if (f->y < 2.5f) { f->y = 2.5f; f->vy = 0; }
    if (f->dtime >= FISH_DIE_T) {
        for (int j = 0; j < 5; j++)
            world_spawn_bubble(w, f->x + rndr(-3, 3), f->y + rndr(-1, 1), rndr(0.9f, 1.8f));
        f->dead = 1;
    }
}

// Is there any food left in this heap?
static int pile_alive(const World *w, int id) {
    if (!id) return 0;
    for (int i = 0; i < MAX_FOOD; i++)
        if (w->food[i].on && w->food[i].pile == id) return 1;
    return 0;
}

// Choose a heap for the fish: the one with the fewest fish already going to it,
// the closest among equals. So the school spreads over the heaps instead of one route.
#define PILE_CROWD 1e4f     // bigger than any distance on the canvas: crowding beats closeness
static int pick_pile(const World *w, const Fish *f) {
    int best_id = 0;
    float best = 1e9f;
    for (int i = 0; i < MAX_FOOD; i++) {
        const Food *fd = &w->food[i];
        if (!fd->on) continue;
        int seen = 0;
        for (int j = 0; j < i; j++)
            if (w->food[j].on && w->food[j].pile == fd->pile) { seen = 1; break; }
        if (seen) continue;
        float near = 1e9f;      // distance to the closest granule of the heap
        for (int j = i; j < MAX_FOOD; j++) {
            const Food *g = &w->food[j];
            if (!g->on || g->pile != fd->pile) continue;
            float d = hypotf(g->x - f->x, g->y - f->y);
            if (d < near) near = d;
        }
        int crowd = 0;
        for (int k = 0; k < w->nfish; k++) {
            const Fish *o = &w->fish[k];
            if (o != f && !o->dying && o->pile == fd->pile) crowd++;
        }
        float score = near + crowd * PILE_CROWD;
        if (score < best) { best = score; best_id = fd->pile; }
    }
    return best_id;
}

void fish_update(World *w, Fish *f, float dt) {
    if (f->dying) { fish_update_dying(w, f, dt); return; }
    f->px = f->x; f->py = f->y;
    const float ymax = world_fish_ymax(w);

    // target: a granule of "its" heap, or wandering
    f->hunting = 0;
    if (!pile_alive(w, f->pile)) f->pile = pick_pile(w, f);
    if (f->pile) {
        float best = 1e9f;
        for (int i = 0; i < MAX_FOOD; i++) {
            const Food *fd = &w->food[i];
            if (!fd->on || fd->pile != f->pile) continue;
            float d = hypotf(fd->x - f->x, fd->y - f->y);
            if (d < best) { best = d; f->tx = fd->x; f->ty = fd->y; f->hunting = 1; }
        }
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

        float dx = w->food[i].x - f->x;
        float dy = w->food[i].y - f->y;

        // Elliptical hitbox matching the fish body.
        if ((dx * dx) / 42.25f + (dy * dy) / 12.25f < 1.0f) {
            w->food[i].on = 0;

            for (int j = 0; j < 4; j++)
                world_spawn_bubble(w,
                    f->x + rndr(-1, 1),
                    f->y + rndr(-1, 1),
                    rndr(0.9f, 1.6f));

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
void food_init(Food *f, float x, float y, int pile) {
    *f = (Food){x, y, rndr(5, 8), 0, 1, pile};
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
    wd->g = rndr(0.7f, 1.0f);   // brightness; color itself comes from the theme
}
