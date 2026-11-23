// ui - text layers over scene: start screen, scrolling text, tags, HUD
#ifndef UI_H
#define UI_H

#include "world.h"

// Call after scene_draw and before gfx_present
// into - show start screen (the scene darkens)
void ui_draw(const World *w, float alpha, int intro, float fps);

#endif
