#ifndef FILES_H
#define FILES_H

#include "window.h"

// "Files": a graphical browser for the VFS. Click selects, double-click
// or Enter opens (directory: enter it, file: show its text), Backspace
// goes up / back to the list, arrows and PgUp/PgDn move or scroll.
void files_init(window_t* win);

#endif
