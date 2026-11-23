#define _POSIX_C_SOURCE 200809L
#include "util.h"

#include <time.h>

static uint32_t rng_s = 2463534242u;

void rng_seed(uint32_t s) {
    rng_s ^= s;
    if (!rng_s) rng_s = 1;
}

float rnd(void) {   // xorshift32
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 17;
    rng_s ^= rng_s << 5;
    return (rng_s >> 8) * (1.0f / 16777216.0f);
}

float rndr(float a, float b) { return a + (b - a) * rnd(); }

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
