#include "theme.h"

#include <math.h>
#include <string.h>

_Static_assert(sizeof(Theme) % sizeof(float) == 0, "Theme must contain floats only");

#define FADE_T 0.8f     // crossfade time, seconds

static const struct { const char *name; Theme t; } THEMES[] = {
    { "ocean", {
        .water_top = {30, 130, 165}, .water_bot = {4, 16, 44}, .grad_pow = 0.7f, .horizon = 1.0f,
        .ray = {80, 100, 120}, .surface = {170, 230, 240},
        .sand = {150, 125, 85}, .grid_floor = {20, 30, 60}, .grid_line = {120, 210, 235}, .grid = 0,
        .sun_a = {255, 230, 120}, .sun_b = {255, 60, 130}, .sun = 0,
        .weed_back = {35, 150, 80}, .weed_front = {20, 90, 50},
        .bubble = {190, 235, 255}, .food = {230, 170, 80},
        .fish = {{255, 140, 40}, {80, 160, 255}, {255, 100, 160}, {110, 220, 110}, {240, 220, 80}, {170, 120, 255}},
        .ui_box = {6, 18, 36}, .ui_frame = {120, 210, 235}, .ui_title = {255, 255, 255},
        .ui_sub = {128, 128, 255}, .ui_key = {255, 215, 110}, .ui_text = {215, 232, 245},
        .ui_dim = {200, 200, 200}, .ui_prompt = {150, 230, 255},
    }},
    { "retrowave", {
        .water_top = {22, 4, 58}, .water_bot = {255, 48, 140}, .grad_pow = 2.2f, .horizon = 0.70f,
        .ray = {110, 40, 160}, .surface = {255, 113, 206},
        .sand = {120, 60, 110}, .grid_floor = {18, 3, 40}, .grid_line = {255, 64, 200}, .grid = 1,
        .sun_a = {255, 235, 110}, .sun_b = {255, 45, 150}, .sun = 1,
        .weed_back = {5, 170, 190}, .weed_front = {150, 50, 200},
        .bubble = {170, 250, 255}, .food = {255, 251, 150},
        .fish = {{255, 113, 206}, {1, 205, 254}, {5, 255, 161}, {185, 103, 255}, {255, 251, 150}, {255, 140, 60}},
        .ui_box = {14, 4, 36}, .ui_frame = {255, 113, 206}, .ui_title = {255, 240, 255},
        .ui_sub = {1, 205, 254}, .ui_key = {255, 251, 150}, .ui_text = {225, 205, 255},
        .ui_dim = {185, 150, 230}, .ui_prompt = {1, 205, 254},
    }},
    { "sunset", {
        .water_top = {235, 120, 80}, .water_bot = {30, 8, 48}, .grad_pow = 0.8f, .horizon = 1.0f,
        .ray = {120, 80, 50}, .surface = {255, 225, 170},
        .sand = {175, 120, 90}, .grid_floor = {40, 12, 40}, .grid_line = {255, 170, 100}, .grid = 0,
        .sun_a = {255, 230, 120}, .sun_b = {255, 60, 130}, .sun = 0,
        .weed_back = {120, 125, 50}, .weed_front = {70, 70, 35},
        .bubble = {255, 235, 210}, .food = {255, 210, 120},
        .fish = {{255, 235, 120}, {70, 210, 210}, {245, 240, 235}, {150, 110, 255}, {255, 90, 100}, {120, 235, 170}},
        .ui_box = {40, 12, 40}, .ui_frame = {255, 170, 100}, .ui_title = {255, 250, 240},
        .ui_sub = {255, 140, 170}, .ui_key = {255, 225, 140}, .ui_text = {255, 225, 205},
        .ui_dim = {225, 190, 175}, .ui_prompt = {255, 190, 130},
    }},
    { "abyss", {
        .water_top = {8, 40, 62}, .water_bot = {0, 3, 10}, .grad_pow = 0.6f, .horizon = 1.0f,
        .ray = {20, 60, 80}, .surface = {80, 160, 190},
        .sand = {40, 52, 58}, .grid_floor = {2, 10, 18}, .grid_line = {0, 200, 170}, .grid = 0,
        .sun_a = {255, 230, 120}, .sun_b = {255, 60, 130}, .sun = 0,
        .weed_back = {10, 90, 90}, .weed_front = {6, 50, 60},
        .bubble = {120, 220, 235}, .food = {120, 255, 200},
        .fish = {{0, 255, 200}, {60, 150, 255}, {255, 50, 190}, {180, 255, 60}, {150, 90, 255}, {255, 140, 40}},
        .ui_box = {2, 10, 18}, .ui_frame = {0, 200, 170}, .ui_title = {220, 255, 250},
        .ui_sub = {60, 150, 255}, .ui_key = {0, 255, 200}, .ui_text = {170, 225, 225},
        .ui_dim = {120, 170, 170}, .ui_prompt = {0, 255, 200},
    }},
    { "matrix", {
        .water_top = {0, 38, 10}, .water_bot = {0, 5, 1}, .grad_pow = 0.7f, .horizon = 1.0f,
        .ray = {0, 90, 25}, .surface = {110, 255, 140},
        .sand = {0, 70, 18}, .grid_floor = {0, 10, 2}, .grid_line = {0, 255, 70}, .grid = 0,
        .sun_a = {255, 230, 120}, .sun_b = {255, 60, 130}, .sun = 0,
        .weed_back = {0, 130, 35}, .weed_front = {0, 75, 22},
        .bubble = {150, 255, 175}, .food = {210, 255, 215},
        .fish = {{0, 255, 70}, {120, 255, 150}, {0, 200, 55}, {190, 255, 205}, {0, 160, 45}, {60, 255, 110}},
        .ui_box = {0, 10, 2}, .ui_frame = {0, 255, 70}, .ui_title = {200, 255, 210},
        .ui_sub = {0, 200, 55}, .ui_key = {190, 255, 205}, .ui_text = {120, 230, 140},
        .ui_dim = {0, 170, 45}, .ui_prompt = {0, 255, 70},
    }},
    { "gruvbox", {
        .water_top = {80, 73, 69}, .water_bot = {29, 32, 33}, .grad_pow = 0.9f, .horizon = 1.0f,
        .ray = {70, 60, 45}, .surface = {235, 219, 178},
        .sand = {146, 131, 116}, .grid_floor = {40, 40, 40}, .grid_line = {250, 189, 47}, .grid = 0,
        .sun_a = {255, 230, 120}, .sun_b = {255, 60, 130}, .sun = 0,
        .weed_back = {104, 157, 106}, .weed_front = {66, 123, 88},
        .bubble = {213, 196, 161}, .food = {254, 128, 25},
        .fish = {{251, 73, 52}, {184, 187, 38}, {250, 189, 47}, {131, 165, 152}, {211, 134, 155}, {254, 128, 25}},
        .ui_box = {40, 40, 40}, .ui_frame = {250, 189, 47}, .ui_title = {235, 219, 178},
        .ui_sub = {142, 192, 124}, .ui_key = {250, 189, 47}, .ui_text = {213, 196, 161},
        .ui_dim = {168, 153, 132}, .ui_prompt = {131, 165, 152},
    }},
};
#define NTHEMES ((int)(sizeof THEMES / sizeof *THEMES))

static Theme cur, from;
static int idx = 0;
static float fade = 1.0f;       // 0..1, 1 - crossfade finished
static float age = 99.0f;

int theme_count(void) { return NTHEMES; }
const char *theme_name(int i) { return THEMES[((i % NTHEMES) + NTHEMES) % NTHEMES].name; }
int theme_idx(void) { return idx; }
const Theme *theme_cur(void) { return &cur; }
float theme_age(void) { return age; }
int theme_changing(void) { return fade < 1.0f; }

int theme_find(const char *name) {
    for (int i = 0; i < NTHEMES; i++)
        if (!strcmp(name, THEMES[i].name)) return i;
    return -1;
}

static void blend(Theme *o, const Theme *a, const Theme *b, float t) {
    const float *pa = (const float *)a, *pb = (const float *)b;
    float *po = (float *)o;
    for (size_t i = 0; i < sizeof(Theme) / sizeof(float); i++) po[i] = lerpf(pa[i], pb[i], t);
}

void theme_set_now(int i) {
    idx = ((i % NTHEMES) + NTHEMES) % NTHEMES;
    cur = from = THEMES[idx].t;
    fade = 1.0f;
}

void theme_set(int i) {
    i = ((i % NTHEMES) + NTHEMES) % NTHEMES;
    if (i == idx) return;
    from = cur;             // start from what is on screen now - no jumps mid-fade
    idx = i;
    fade = 0.0f;
    age = 0.0f;
}

void theme_step(int dir) { theme_set(idx + dir); }

void theme_update(float dt) {
    age += dt;
    if (fade >= 1.0f) return;
    fade += dt / FADE_T;
    if (fade >= 1.0f) { fade = 1.0f; cur = THEMES[idx].t; return; }
    float s = fade * fade * (3.0f - 2.0f * fade);   // smoothstep
    blend(&cur, &from, &THEMES[idx].t, s);
}

void theme_vt_palette(const Theme *t, Col p[16]) {
    for (int i = 0; i < 5; i++)
        p[i] = lerpc(t->water_top, t->water_bot, powf(i / 4.0f, t->grad_pow));
    p[5] = lerpc(scalec(t->sand, 0.85f), t->grid_floor, t->grid);
    p[6] = lerpc(scalec(t->sand, 0.55f), t->grid_line, t->grid);
    p[7] = t->weed_front;
    p[8] = t->weed_back;
    for (int i = 0; i < THEME_FISH; i++) p[9 + i] = t->fish[i];
    p[15] = t->ui_title;
}
