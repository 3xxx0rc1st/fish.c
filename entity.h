// entity - fish, bubbles, food, seaweed
#ifndef ENTITY_H
#define ENTITY_H

#include "sprite.h"
#include "util.h"

#define MAX_FISH 8                  // alive at once
#define FISH_SLOTS (MAX_FISH * 2)   // + those that are still dying
#define FISH_DIE_T 2.6f             // death animation, seconds
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
    int pile;           // heap of food this fish goes to (0 - none), kept until the heap is gone
    int ci;             // color index in the theme (palette is taken at draw time)
    int dying, dead;    // dying: floats up belly-up, dead: remove from the world
    float dtime;        // seconds since death
    const char *name;
} Fish;

typedef struct {
    float x, y, px, py, x0, vy, ph, fr, amp, r, gr, pop;
    int on;
} Bubble;

typedef struct { float x, y, vy, age; int on; int pile; } Food;   // pile: id of the heap it was dropped in

typedef struct { float fx, hf, ph, amp, g; int front; } Weed;

void fish_init(Fish *f, const World *w);
void fish_kill(World *w, Fish *f);          // start dying
void fish_update(World *w, Fish *f, float dt);

void bubble_init(Bubble *b, float x, float y, float r);
void bubble_update(Bubble *b, float dt);

void food_init(Food *f, float x, float y, int pile);
void food_update(const World *w, Food *f, float dt);

void weed_init(Weed *wd, int i, int n);

#endif
