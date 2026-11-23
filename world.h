// world - aquarium state and simulation
#ifndef WORLD_H
#define WORLD_H

#include "entity.h"

struct World {
    Fish    fish[MAX_FISH];
    int     nfish;
    Bubble  bub[MAX_BUB];
    Food    food[MAX_FOOD];
    Weed    weed[MAX_WEED];
    int     nweed;
    float   t;          // world's time, seconds
    float   vent_t[3];  // bottom bubble sources' timers
    int     w, h;
};

static inline float world_floor_y(const World *w)   { return (float)(w->h - 3); }       // top of the sand
static inline float world_fish_ymax(const World *w)   { return (float)(w->h - 7); }    // fish doesn't swim under sand

void world_init(World *w, int pw, int ph);
void world_resize(World *w, int pw, int ph);    // clamps entities for new size
void world_update(World *w, float dt);          // one fixed step

void world_spawn_bubble(World *w, float x, float y, float r);
void world_bubble_burst(World *w, int n);      // n bubbles from bottom
void world_drop_food(World *w, float x, float y);
void world_drop_food_burst(World *w, float x, float y); // 3 granules in a small pile
void world_add_fish(World *w);

#endif
