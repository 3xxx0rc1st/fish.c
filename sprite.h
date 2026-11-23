// sprite - pixel fish sprite and its render on canvas
#ifndef SPRITE_H
#define SPRITE_H

#include "util.h"

#define SPR_W 14
#define SPR_H 7

typedef struct { Col body, tail, fin; } FishPalette;

// Draws a fish with center in (cx, cy) in float coords.
// face - [-1..1]: -1 looks left, +1 looks right; sprite shinks inbetween them (turn)
// phase - tail wagging phase (radians)
// Reverse mapping + bilinear sampling: no per-frame sprites
void sprite_draw_fish(const FishPalette *pal, float cx, float cy, float face, float phase);

#endif
