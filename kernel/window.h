#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>

#define MAX_WINDOWS     8
#define TITLEBAR_H      20
#define WIN_BORDER      1
#define RESIZE_ZONE     6

// Colors
#define WIN_COLOR_BG        0x2D2D2D
#define WIN_COLOR_BORDER    0x555555
#define WIN_COLOR_TITLEBAR  0x0078D7
#define WIN_COLOR_TITLE_TXT 0xFFFFFF
#define WIN_COLOR_CLOSE_BTN 0xE81123
#define DESKTOP_COLOR       0x1A3A5C

typedef struct window {
    int      x, y;          // Position
    int      w, h;          // Size (includes title bar)
    char     title[64];
    int      visible;
    int      focused;
    void   (*on_paint)(struct window* self);   // optional — called every redraw
    void   (*on_resize)(struct window* self);  // optional — called on mouse release after resize
    // optional — left click in the client area; (x, y) relative to its
    // top-left corner (just below the title bar)
    void   (*on_click)(struct window* self, int x, int y);
    // optional — keys typed while this window is focused
    void   (*on_key)(struct window* self, char c);
    int      min_w, min_h;                     // minimum dimensions enforced during resize
} window_t;

void wm_init();
window_t* wm_create(int x, int y, int w, int h, const char* title);
void wm_draw_all();
void wm_draw_desktop();
window_t* wm_get_focused();
void wm_hide(window_t* win);   // Like the close button: reopen from the menu

// Input — call these from the main loop
void wm_on_mouse_move(int x, int y);
void wm_on_mouse_click(int x, int y, uint8_t buttons);

#endif
