// entity - fish, bubbles, food, seaweed
#ifndef ENTITY_H
#define ENTITY_H

#include "sprite.h"
#include "util.h"

#define MAX_FISH 8
#define MAX_BUB 192
#define MAX_FOOD 24
#define MAX_WEED 18
#define POP_T 0.18f

typedef struct World World;

typedef struct {
    float x, y, px, py;
    float vx, vy, tx, ty, tt;
    float phase, face, spd, bt;
    int dir, hunting;
    FishPalette pal;
    const char *name;
} Fish;

typedef struct {
    float x, y, px, py, x0, vy, ph, fr, amp, r, gr, pop;
    int on;
} Bubble;

typedef struct { float x, y, vy, age; int on; } Food;

typedef struct { float fx, hf, ph, amp; int front; Col c; } Weed;

void fish_init(Fish *f, const World *w, int index);
void fish_update(World *w, Fish *f, float dt);

void bubble_init(Bubble *b, float x, float y, float r);
void bubble_update(Bubble *b, float dt);

void food_init(Food *f, float x, float y);
void food_update(const World *w, Food *f, float dt);

void weed_init(Weed *wd, int i, int n);

#endif
