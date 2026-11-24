// shell - a real shell (pty) in a translucent panel over the aquarium
//
// $SHELL runs on a pseudo-terminal, its output goes through a small terminal
// emulator (xterm subset: colors, cursor, scroll regions, alt-screen, scrollback)
// and is drawn over the water with text layer 2; fish keep swimming behind the glass.
// The shell survives hiding the panel; it ends when it exits or the aquarium quits.
#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>

int  shell_visible(void);
int  shell_running(void);
void shell_toggle(void);                    // show / hide (starts the shell on first show)
void shell_resize(void);                    // host terminal changed size (gfx_resize already done)
int  shell_fd(void);                        // pty master for poll(), -1 if none
void shell_pump(void);                      // pty -> emulator, reap the child
void shell_input(const char *b, size_t n);  // keystrokes while the panel is visible
void shell_draw(void);                      // after scene_draw; panel + text layer 2
void shell_shutdown(void);                  // HUP + close; safe from a crash handler

// Panel keys: F12 or Ctrl-] hide it; Shift-PgUp/PgDn and mouse wheel scroll history.
#endif
