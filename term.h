// term - raw mode, signals. window size, output buffer
#ifndef TERM_H
#define TERM_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

extern volatile sig_atomic_t term_quit;     // SIGINT/SIGTERM/SIGHUP or 'q'
extern volatile sig_atomic_t term_resized;  // SIGWINCH (starts with 1)
extern int term_cols, term_rows;

void term_init(void);       // raw mode, alt-screen, mouse, signal handlers
void term_restore(void);    // idempotent; hangs on atexit and crashes
void term_size(void);       // update term_cols/term_rows
// on: keys go to the shell as they are (no Ctrl-C/Z/\ signals, no XON/XOFF, Enter = CR); off: aquarium mode
void term_shell_mode(int on);

// Output buffer: frame renders whole and goes in one write()
void ob_reset(void);
void ob_str(const char *s, size_t n);
void ob_c(char c);
void ob_num(unsigned v);
void ob_cp(uint32_t cp);    // codepoint -> UTF-8
void ob_flush(void);
#define OB_LIT(s) ob_str(s, sizeof(s) - 1)

#endif