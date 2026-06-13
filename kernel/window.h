#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>

#define MAX_WINDOWS     8
#define TITLEBAR_H      20
#define WIN_BORDER      1

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
    // Optional: per-window paint callback
    void   (*on_paint)(struct window* self);
} window_t;

void wm_init();
window_t* wm_create(int x, int y, int w, int h, const char* title);
void wm_draw_all();
void wm_draw_desktop();
window_t* wm_get_focused();

// Input — call these from the main loop
void wm_on_mouse_move(int x, int y);
void wm_on_mouse_click(int x, int y, uint8_t buttons);

#endif
