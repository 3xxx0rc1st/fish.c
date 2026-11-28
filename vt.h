// vt - Linux virtual console (TERM=linux, /dev/ttyN): temporary tweaks
//
// What is wrong with the bare console:
//  - it has no truecolor: 38;2/48;2 are squashed to 8 hues by a crude threshold;
//  - 16 fixed colors, and only 8 of them can be a background;
//  - with a 512-glyph font bright colors are gone (intensity bit = 9th glyph bit);
//  - a font may lack half-blocks / box drawing (typical for cyrillic fonts).
// So while aquash is running we:
//  1. save the font, load a 256-glyph one (same glyphs + our own in slots 1..9);
//  2. reprogram the 16-color palette from the current theme (OSC P);
//  3. render with palette indices + ordered dithering (see gfx.c).
// Everything is restored in vt_restore() (atexit, signals, crash).
// Glyph slots are reached through the "direct-to-font" zone: U+F000 + slot.
#ifndef VT_H
#define VT_H

#include <stdint.h>

extern int vt_active;       // 1 - console mode: palette indices instead of truecolor

#define VT_G_UP     0xF001u     // ▀
#define VT_G_LO     0xF002u     // ▄
#define VT_G_FULL   0xF003u     // █
#define VT_G_TL     0xF004u     // ┌
#define VT_G_TR     0xF005u     // ┐
#define VT_G_BL     0xF006u     // └
#define VT_G_BR     0xF007u     // ┘
#define VT_G_H      0xF008u     // ─
#define VT_G_V      0xF009u     // │

// enable = 0: do not touch the console (-T). AQUASH_VT=0 disables it too,
// AQUASH_VT=force enables the renderer without ioctl (test hook, any terminal).
int      vt_init(int enable);
void     vt_restore(void);      // idempotent, async-signal-safe enough for crash handler

// codepoint from the UI -> what to actually print in console mode
uint32_t vt_map_cp(uint32_t cp);

#endif
