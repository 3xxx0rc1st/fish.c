// theme - aquarium color palettes, smooth crossfade between them
//
// Theme consists of floats only (Col = 3 floats), so two themes are blended
// field by field: colors, and also "weights" (grid, sun) - decor fades in/out.
#ifndef THEME_H
#define THEME_H

#include "util.h"

#define THEME_FISH 6

typedef struct {
    // water: vertical gradient down to the horizon
    Col   water_top, water_bot;
    float grad_pow;         // gradient curve: <1 - reaches bottom color fast, >1 - slowly
    float horizon;          // horizon height, fraction of the canvas (1 - no floor perspective)
    Col   ray, surface;     // light rays tint, glint on the surface
    // floor: sand or neon grid (grid = blend weight)
    Col   sand;
    Col   grid_floor, grid_line;
    float grid;
    // striped sun (retrowave), sun = weight
    Col   sun_a, sun_b;
    float sun;
    // inhabitants
    Col   weed_back, weed_front;
    Col   bubble, food;
    Col   fish[THEME_FISH];
    // ui
    Col   ui_box, ui_frame, ui_title, ui_sub, ui_key, ui_text, ui_dim, ui_prompt;
} Theme;

int         theme_count(void);
const char *theme_name(int i);
int         theme_find(const char *name);   // -1 if there is no such theme
int         theme_idx(void);

void        theme_set_now(int i);           // instantly (start)
void        theme_set(int i);               // smooth crossfade
void        theme_step(int dir);            // +1 / -1, cyclic
void        theme_update(float dt);         // call every frame
int         theme_changing(void);           // crossfade in progress
float       theme_age(void);                // seconds since the last switch
const Theme *theme_cur(void);               // current (blended) theme

// 16 colors for the Linux console: 0-7 may be backgrounds (water ramp, floor),
// 8-15 are foreground only (weeds, fish, white)
void        theme_vt_palette(const Theme *t, Col out[16]);

#endif
