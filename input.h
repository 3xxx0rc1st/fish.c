// input — non-blocking keyboard and mouse reading (SGR protocol)
#ifndef INPUT_H
#define INPUT_H

#include "world.h"

// Reads everything accumulated in stdin and applies it to the world.
// *intro is reset to 0 upon any key press or mouse click; 'q' sets term_quit.
void input_poll(World *w, int *intro);

#endif
