// util - color, math, RNG, time
#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

typedef struct { float r, g, b; } Col;

static inline Col col(float r, float g, float b) { Col c = {r, g, b}; return c; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline Col scalec(Col c, float k) { return col(c.r * k, c.g * k, c.b * k); }
static inline Col lerpc(Col a, Col b, float t) {
    return col(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t);
}

void        rng_seed(uint32_t s);
float       rnd(void);                  // [0, 1)
float       rndr(float a, float b);     // [a, b]
uint32_t    hash32(uint32_t x);
double      now_s(void);                // CLOCK_MONOTONIC, seconds

#endif
