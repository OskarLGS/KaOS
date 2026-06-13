#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

typedef struct {
    uint32_t framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t is_graphical;
} vbe_info_t;

void graphics_init();
int is_graphical_mode();
void draw_pixel(int x, int y, uint32_t color);
void clear_screen(uint32_t color);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_char(int x, int y, char c, uint32_t color);
void draw_string(int x, int y, const char* str, uint32_t color);
void graphics_scroll(int start_y, int scroll_amount);
void swap_buffers();

// Cursor
void cursor_enable(int enabled);
void cursor_set_pos(int x, int y);

#endif
