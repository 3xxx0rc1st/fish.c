// sprite - pixel fish sprite and its render on canvas
#ifndef SPRITE_H
#define SPRITE_H

#include "util.h"

#define SPR_W 14
#define SPR_H 7
#define SPR_KINDS 5     // sprite variants, see sprite.c

typedef struct { Col body, tail, fin; } FishPalette;

// Draws a fish of the given kind (0..SPR_KINDS-1, only the look differs) with center in (cx, cy) in float coords.
// face - [-1..1]: -1 looks left, +1 looks right; sprite shinks inbetween them (turn)
// phase - tail wagging phase (radians)
// roll - [-1..1]: 1 - normal, -1 - belly up (squashes through 0, like the turn)
// opacity - [0..1], for dying fish
// Reverse mapping + bilinear sampling: no per-frame sprites
void sprite_draw_fish(int kind, const FishPalette *pal, float cx, float cy, float face, float phase,
                       float roll, float opacity);

#endif
