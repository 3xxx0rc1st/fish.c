// kgp - optional Kitty graphics protocol backend.
//
// The canvas goes to the terminal as ONE image per frame, handed over through POSIX shared memory
// (the terminal reads and deletes the object), so almost nothing travels through the pty and the
// terminal does not parse hundreds of KB of SGR per second. Text (HUD, names, shell panel) stays
// real text on top of the image: the image has a negative z-index and the cells have the default
// background.
//
// Local terminals only: shared memory is the one transport that wins anything (measured: sending
// the same pixels through the escape stream costs about as much as the half-block stream does).
// Everything else (ssh, tmux, no support) falls back to the half-block renderer in gfx.c.
#ifndef KGP_H
#define KGP_H

#include "util.h"

typedef enum { KGP_AUTO, KGP_OFF, KGP_ON } KgpMode;

extern int kgp_active;      // 1: gfx_present sends the canvas as an image

// After term_init(), before the first frame. AUTO asks the terminal (kitty graphics query + a
// shared-memory probe, DA1 as the end marker); ON skips the question; OFF does nothing.
void kgp_init(KgpMode mode);

// Puts the canvas (w x h, float RGB; w = cols, h = 2 * rows) into a fresh shared-memory object, scaled
// with nearest neighbour to exactly the window's pixels (cell size: see cell_size() in kgp.c; override
// with FISH_CELL_SIZE=WxH). The terminal would stretch a smaller image with a smooth filter and blur
// the pixel art. 0 = failed: the backend switches itself off and gfx_present falls back to half-blocks.
int  kgp_frame(const Col *px, int w, int h, int cols, int rows);
// Appends the commands for the frame prepared by kgp_frame() to the output buffer: show the new
// image over cols x rows cells, delete the previous one.
void kgp_emit(int cols, int rows);
// "Delete every image" command, appended when the backend was just switched off or the screen
// was cleared.
void kgp_emit_clear(void);

// Delete the image, unlink objects the terminal did not take. Idempotent (term_restore calls it).
void kgp_shutdown(void);

#endif
