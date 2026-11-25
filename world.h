// world - aquarium state and simulation
#ifndef WORLD_H
#define WORLD_H

#include <math.h>
#include "entity.h"

struct World {
    Fish    fish[FISH_SLOTS];
    int     nfish;      // slots in use (alive + dying)
    int     pile_serial;    // id of the last food heap
    int     spawned;    // how many fish were ever added: color of the next one
    Bubble  bub[MAX_BUB];
    Food    food[MAX_FOOD];
    Weed    weed[MAX_WEED];
    int     nweed;
    double  t;          // world's time, seconds. double on purpose: a float stops resolving 1/60 s
                        // steps after ~12 h and freezes after ~6 days (ulp grows with t)
    float   vent_t[3];  // bottom bubble sources' timers
    int     w, h;
};

static inline float world_floor_y(const World *w)   { return (float)(w->h - 3); }       // top of the sand
// phase in radians, in [0, 2*pi), of a wave sin(freq * t): reduced in double, so it stays exact at any uptime
static inline float world_phase(const World *w, double freq) { return (float)fmod(w->t * freq, 6.283185307179586); }
// fractional part of t * freq (a pattern with period 1 / freq)
static inline float world_frac(const World *w, double freq)  { double v = w->t * freq; return (float)(v - floor(v)); }

static inline float world_fish_ymax(const World *w)   { return (float)(w->h - 7); }    // fish doesn't swim under sand

void world_init(World *w, int pw, int ph);
void world_resize(World *w, int pw, int ph);    // clamps entities for new size
void world_update(World *w, float dt);          // one fixed step

void world_spawn_bubble(World *w, float x, float y, float r);
void world_bubble_burst(World *w, int n);      // n bubbles from bottom
void world_drop_food(World *w, float x, float y, int pile);
void world_drop_food_burst(World *w, float x, float y); // 3 granules = one heap (fish pick heaps)
void world_add_fish(World *w);
int  world_fish_alive(const World *w);
int  world_kill_fish(World *w);                         // random living fish; 0 if none
int  world_kill_fish_at(World *w, float x, float y);    // fish under the point (canvas coords)

#endif
