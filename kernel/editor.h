#ifndef EDITOR_H
#define EDITOR_H

#include "window.h"

// A small text editor: `edit <path>` in the shell. Full screen in text
// mode; in the desktop it lives in the "Editor" window. Keys: arrows,
// Home/End, PgUp/PgDn, Backspace/Del, Enter, Tab (4 spaces),
// Ctrl+S save, Ctrl+Q close (twice to discard unsaved changes).

// Desktop: the window the editor draws into (hidden until used)
void editor_attach_window(window_t* win);

// Open `path` for the current user. Returns 0, or prints why not.
int  editor_open(const char* path);
int  editor_active();               // Text mode: the editor owns the screen
void editor_key(char c);

#endif
