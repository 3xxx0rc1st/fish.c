// scene - draws the world on gfx canvas (background, bottom, seaweed, fish, food, bubbles)
#ifndef SCENE_H
#define SCENE_H

#include "world.h"

// alpha in [0,1) - fraction of the simulation step that has elapsed since the last world_update (interpolation)
void scene_draw(const World *w, float alpha);

#endif
