#ifndef TERMINAL_H
#define TERMINAL_H

#include "window.h"

// Graphical terminal: hosts the shell inside a desktop window.
void terminal_init(window_t* win);
void terminal_handle_key(char c);
void terminal_on_resize(window_t* w);

// The window this terminal is bound to (0 if not initialized)
window_t* terminal_window();

#endif
