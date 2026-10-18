#include "graphics.h"
#include "font.h"
#include "memory.h"
#include "util.h"

static vbe_info_t vbe;
static uint32_t* back_buffer = 0;

// ─── Software cursor ──────────────────────────────────────────────────────────
// 12×20 arrow — 1 = white, 2 = black outline, 0 = transparent
#define CUR_W 12
#define CUR_H 20
static const uint8_t cursor_shape[CUR_H][CUR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,2,1,0,1,2,2,1,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,2,2,1,0},
    {0,0,0,0,0,0,0,1,2,2,1,0},
    {0,0,0,0,0,0,0,0,1,1,0,0},
};

static int cursor_x_pos  = 400;
static int cursor_y_pos  = 300;
static int cursor_active = 0;

void cursor_enable(int enabled) { cursor_active = enabled; }
void cursor_set_pos(int x, int y) { cursor_x_pos = x; cursor_y_pos = y; }

// Pixels hidden under the cursor, saved so they can be restored after the
// frame is presented (keeps the back buffer pristine for incremental drawing)
static uint32_t cursor_under[CUR_H][CUR_W];

static void draw_cursor_on_backbuffer() {
    for (int row = 0; row < CUR_H; row++) {
        for (int col = 0; col < CUR_W; col++) {
            int px = cursor_x_pos + col;
            int py = cursor_y_pos + row;
            if (px < 0 || px >= (int)vbe.width) continue;
            if (py < 0 || py >= (int)vbe.height) continue;
            uint32_t* p = &back_buffer[py * vbe.width + px];
            cursor_under[row][col] = *p;
            uint8_t v = cursor_shape[row][col];
            if (v != 0) *p = (v == 1) ? 0xFFFFFF : 0x000000;
        }
    }
}

static void restore_under_cursor() {
    for (int row = 0; row < CUR_H; row++) {
        for (int col = 0; col < CUR_W; col++) {
            int px = cursor_x_pos + col;
            int py = cursor_y_pos + row;
            if (px < 0 || px >= (int)vbe.width) continue;
            if (py < 0 || py >= (int)vbe.height) continue;
            back_buffer[py * vbe.width + px] = cursor_under[row][col];
        }
    }
}

// ─── Init ─────────────────────────────────────────────────────────────────────

void graphics_init() {
    vbe_info_t* info = (vbe_info_t*)0x0900;
    memory_copy((char*)info, (char*)&vbe, sizeof(vbe_info_t));

    if (!vbe.is_graphical) return;

    uint32_t buffer_size = vbe.width * vbe.height * (vbe.bpp / 8);
    back_buffer = (uint32_t*)kmalloc(buffer_size);
    if (back_buffer) {
        memory_set((uint8_t*)back_buffer, 0, buffer_size);
    }
}

int is_graphical_mode() {
    return (int)vbe.is_graphical;
}

// ─── Drawing primitives ───────────────────────────────────────────────────────

// Fill `count` dwords with `val` using rep stosl — the per-pixel loops were
// the main cause of visible flicker (full clear + redraw every frame at -O0).
static void fill32(uint32_t* dst, uint32_t val, uint32_t count) {
    __asm__ __volatile__("cld; rep stosl"
                         : "+D"(dst), "+c"(count)
                         : "a"(val) : "memory");
}

// Pointer to the start of row y on the active draw target
static uint32_t* row_ptr(int y) {
    if (back_buffer) return back_buffer + y * vbe.width;
    return (uint32_t*)vbe.framebuffer + y * (vbe.pitch / 4);
}

void draw_pixel(int x, int y, uint32_t color) {
    if (!vbe.is_graphical) return;
    if (x < 0 || x >= (int)vbe.width || y < 0 || y >= (int)vbe.height) return;

    row_ptr(y)[x] = color;
}

void clear_screen(uint32_t color) {
    if (!vbe.is_graphical) return;
    if (back_buffer) {
        fill32(back_buffer, color, vbe.width * vbe.height);
    } else {
        for (int y = 0; y < (int)vbe.height; y++)
            fill32(row_ptr(y), color, vbe.width);
    }
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!vbe.is_graphical) return;

    // Clip to screen so rows can be filled without per-pixel checks
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)vbe.width)  w = vbe.width - x;
    if (y + h > (int)vbe.height) h = vbe.height - y;
    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < h; i++)
        fill32(row_ptr(y + i) + x, color, w);
}

void draw_char(int x, int y, char c, uint32_t color) {
    if ((uint8_t)c < 32 || (uint8_t)c > 126) c = '?'; // Non-printable
    const uint8_t* glyph = font_bitmap[(uint8_t)c];

    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            if (glyph[i] & (1 << (7 - j)))
                draw_pixel(x + j, y + i, color);
}

void draw_string(int x, int y, const char* str, uint32_t color) {
    int cur_x = x;
    while (*str) {
        draw_char(cur_x, y, *str, color);
        cur_x += 8;
        str++;
    }
}

// Same 8x8 font, each pixel drawn as a scale x scale block (big text)
void draw_string_scaled(int x, int y, const char* str, uint32_t color, int scale) {
    for (; *str; str++, x += 8 * scale) {
        char c = *str;
        if ((uint8_t)c < 32 || (uint8_t)c > 126) c = '?';
        const uint8_t* glyph = font_bitmap[(uint8_t)c];
        for (int i = 0; i < 8; i++)
            for (int j = 0; j < 8; j++)
                if (glyph[i] & (1 << (7 - j)))
                    draw_rect(x + j * scale, y + i * scale, scale, scale, color);
    }
}

void graphics_scroll(int start_y, int scroll_amount) {
    if (!back_buffer) return;
    int bytes_per_line = vbe.width * (vbe.bpp / 8);
    int copy_height = vbe.height - start_y - scroll_amount;

    char* dst = (char*)back_buffer + (start_y * bytes_per_line);
    char* src = (char*)back_buffer + ((start_y + scroll_amount) * bytes_per_line);
    memory_copy(src, dst, copy_height * bytes_per_line);

    draw_rect(0, vbe.height - scroll_amount, vbe.width, scroll_amount, 0x1E1E1E);
}

// ─── Buffer swap (with cursor composited on top) ──────────────────────────────

void swap_buffers() {
    if (!back_buffer) return;
    uint32_t* fb = (uint32_t*)vbe.framebuffer;
    uint32_t row_bytes = vbe.width * (vbe.bpp / 8);

    // Composite the cursor into the back buffer BEFORE the copy. Drawing it
    // on the framebuffer after the copy left a window where the cursor was
    // absent from the visible frame — that's what made it flicker.
    if (cursor_active) draw_cursor_on_backbuffer();

    // Copy row by row using the actual pitch (handles framebuffers with padding)
    for (uint32_t y = 0; y < vbe.height; y++) {
        char* dst = (char*)fb + y * vbe.pitch;
        char* src = (char*)back_buffer + y * row_bytes;
        memory_copy(src, dst, row_bytes);
    }

    if (cursor_active) restore_under_cursor();
}
