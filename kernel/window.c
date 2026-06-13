#include "window.h"
#include "graphics.h"
#include "util.h"
#include "mouse.h"

static window_t windows[MAX_WINDOWS];
static int      win_count   = 0;
static int      focused_win = -1;

// Dragging state
static int drag_active = 0;
static int drag_win    = -1;
static int drag_off_x  = 0;
static int drag_off_y  = 0;

static int z_order[MAX_WINDOWS];
static int z_count = 0;

// Taskbar / start menu layout
#define TASKBAR_H    28
#define TASKBAR_Y    (600 - TASKBAR_H)
#define START_BTN_W  56
#define TASK_BTN_W   110
#define TASK_BTN_GAP 4
#define MENU_W       150
#define MENU_ITEM_H  24

static int start_menu_open = 0;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void draw_window(int idx) {
    window_t* w = &windows[idx];
    if (!w->visible) return;

    int focused = (idx == focused_win);

    // Background
    draw_rect(w->x, w->y + TITLEBAR_H, w->w, w->h - TITLEBAR_H, WIN_COLOR_BG);

    // Border
    // Top edge of client area
    draw_rect(w->x, w->y + TITLEBAR_H - WIN_BORDER, w->w, WIN_BORDER, WIN_COLOR_BORDER);
    // Bottom
    draw_rect(w->x, w->y + w->h, w->w, WIN_BORDER, WIN_COLOR_BORDER);
    // Left
    draw_rect(w->x - WIN_BORDER, w->y, WIN_BORDER, w->h + WIN_BORDER, WIN_COLOR_BORDER);
    // Right
    draw_rect(w->x + w->w, w->y, WIN_BORDER, w->h + WIN_BORDER, WIN_COLOR_BORDER);

    // Title bar
    uint32_t tb_color = focused ? WIN_COLOR_TITLEBAR : 0x444444;
    draw_rect(w->x, w->y, w->w, TITLEBAR_H, tb_color);

    // Title text (centered vertically in bar)
    draw_string(w->x + 6, w->y + 6, w->title, WIN_COLOR_TITLE_TXT);

    // Close button — red square top-right
    int btn_x = w->x + w->w - 18;
    int btn_y = w->y + 3;
    draw_rect(btn_x, btn_y, 14, 14, WIN_COLOR_CLOSE_BTN);
    draw_string(btn_x + 3, btn_y + 3, "x", WIN_COLOR_TITLE_TXT);

    // Let the window paint its own content if it has a callback
    if (w->on_paint) w->on_paint(w);
}

static void wm_bring_to_front(int idx) {
    int pos = -1;
    for (int i = 0; i < z_count; i++)
        if (z_order[i] == idx) { pos = i; break; }
    if (pos < 0 || pos == z_count - 1) return;
    for (int i = pos; i < z_count - 1; i++)
        z_order[i] = z_order[i + 1];
    z_order[z_count - 1] = idx;
}

// ─── Public API ───────────────────────────────────────────────────────────────

void wm_init() {
    win_count   = 0;
    z_count     = 0;
    focused_win = -1;
    drag_active = 0;
}

window_t* wm_get_focused() {
    if (focused_win < 0) return 0;
    return &windows[focused_win];
}

window_t* wm_create(int x, int y, int w, int h, const char* title) {
    if (win_count >= MAX_WINDOWS) return 0;

    window_t* win = &windows[win_count];
    win->x       = x;
    win->y       = y;
    win->w       = w;
    win->h       = h;
    win->visible = 1;
    win->focused = 0;
    win->on_paint = 0;
    win->on_resize = 0;
    win->min_w     = 200;
    win->min_h     = TITLEBAR_H + 60;

    int i = 0;
    while (title[i] && i < 63) { win->title[i] = title[i]; i++; }
    win->title[i] = '\0';

    focused_win = win_count;
    win->focused = 1;
    z_order[z_count++] = win_count;
    win_count++;
    return win;
}

void wm_draw_desktop() {
    clear_screen(DESKTOP_COLOR);

    // Taskbar
    draw_rect(0, TASKBAR_Y, 800, TASKBAR_H, 0x1A1A1A);

    // Start button (highlighted while the menu is open)
    draw_rect(0, TASKBAR_Y, START_BTN_W, TASKBAR_H,
              start_menu_open ? WIN_COLOR_TITLEBAR : 0x2A2A2A);
    draw_string(12, TASKBAR_Y + 10, "KaOS", 0xFFFFFF);

    // One button per window: blue = focused, gray = visible, dark = hidden
    int bx = START_BTN_W + TASK_BTN_GAP;
    for (int i = 0; i < win_count; i++) {
        uint32_t bg = !windows[i].visible ? 0x222222
                    : (i == focused_win)  ? WIN_COLOR_TITLEBAR
                    :                       0x3C3C3C;
        draw_rect(bx, TASKBAR_Y + 2, TASK_BTN_W, TASKBAR_H - 4, bg);

        char t[14];
        int n = 0;
        while (windows[i].title[n] && n < 13) { t[n] = windows[i].title[n]; n++; }
        t[n] = '\0';
        draw_string(bx + 6, TASKBAR_Y + 10, t,
                    windows[i].visible ? 0xFFFFFF : 0x888888);

        bx += TASK_BTN_W + TASK_BTN_GAP;
    }
}

static void draw_start_menu() {
    if (!start_menu_open || win_count == 0) return;

    int h  = win_count * MENU_ITEM_H;
    int y0 = TASKBAR_Y - h;

    draw_rect(0, y0 - 1, MENU_W + 1, 1, WIN_COLOR_BORDER);   // Top border
    draw_rect(MENU_W, y0 - 1, 1, h + 1, WIN_COLOR_BORDER);   // Right border
    draw_rect(0, y0, MENU_W, h, WIN_COLOR_BG);

    for (int i = 0; i < win_count; i++) {
        int iy = y0 + i * MENU_ITEM_H;
        draw_string(10, iy + 8, windows[i].title,
                    windows[i].visible ? 0xFFFFFF : 0x9A9A9A);
    }
}

void wm_draw_all() {
    wm_draw_desktop();
    for (int i = 0; i < z_count; i++) {
        draw_window(z_order[i]);
    }
    draw_start_menu(); // On top of everything
    swap_buffers();
}

// ─── Input handling ───────────────────────────────────────────────────────────

void wm_on_mouse_move(int x, int y) {
    cursor_set_pos(x, y);

    if (drag_active && drag_win >= 0) {
        windows[drag_win].x = x - drag_off_x;
        windows[drag_win].y = y - drag_off_y;
        // Clamp to screen
        if (windows[drag_win].x < 0) windows[drag_win].x = 0;
        if (windows[drag_win].y < 0) windows[drag_win].y = 0;
        if (windows[drag_win].x + windows[drag_win].w > 800)
            windows[drag_win].x = 800 - windows[drag_win].w;
        if (windows[drag_win].y + windows[drag_win].h > 572)
            windows[drag_win].y = 572 - windows[drag_win].h;
    }
}

static void activate_window(int i) {
    windows[i].visible = 1;
    if (focused_win >= 0) windows[focused_win].focused = 0;
    focused_win = i;
    windows[i].focused = 1;
    wm_bring_to_front(i);
}

static void minimize_window(int i) {
    windows[i].visible = 0;
    windows[i].focused = 0;
    focused_win = -1;
    for (int j = z_count - 1; j >= 0; j--) {
        int jdx = z_order[j];
        if (windows[jdx].visible) { focused_win = jdx; windows[jdx].focused = 1; break; }
    }
}

void wm_on_mouse_click(int x, int y, uint8_t buttons) {
    if (buttons & MOUSE_LEFT) {
        // 1. Start menu items (menu sits on top of everything)
        if (start_menu_open) {
            int h  = win_count * MENU_ITEM_H;
            int y0 = TASKBAR_Y - h;
            if (x < MENU_W && y >= y0 && y < TASKBAR_Y) {
                int idx = (y - y0) / MENU_ITEM_H;
                if (idx >= 0 && idx < win_count) activate_window(idx);
                start_menu_open = 0;
                return;
            }
        }

        // 2. Taskbar
        if (y >= TASKBAR_Y) {
            if (x < START_BTN_W) {
                start_menu_open = !start_menu_open;
                return;
            }
            int bx = START_BTN_W + TASK_BTN_GAP;
            for (int i = 0; i < win_count; i++) {
                if (x >= bx && x < bx + TASK_BTN_W) {
                    // Click on the focused window's button minimizes it
                    if (windows[i].visible && focused_win == i)
                        minimize_window(i);
                    else
                        activate_window(i);
                    start_menu_open = 0;
                    return;
                }
                bx += TASK_BTN_W + TASK_BTN_GAP;
            }
            start_menu_open = 0;
            return;
        }

        // 3. Click anywhere else closes the menu and falls through to windows
        start_menu_open = 0;

        // Check windows front-to-back via z_order
        for (int i = z_count - 1; i >= 0; i--) {
            int idx = z_order[i];
            window_t* w = &windows[idx];
            if (!w->visible) continue;

            // Hit test the window
            if (x >= w->x && x < w->x + w->w &&
                y >= w->y && y < w->y + w->h) {

                // Focus
                if (focused_win != idx) {
                    if (focused_win >= 0) windows[focused_win].focused = 0;
                    focused_win = idx;
                    w->focused  = 1;
                }
                wm_bring_to_front(idx);

                // Close button?
                int btn_x = w->x + w->w - 18;
                int btn_y = w->y + 3;
                if (x >= btn_x && x < btn_x + 14 &&
                    y >= btn_y && y < btn_y + 14) {
                    minimize_window(idx);
                    return;
                }

                // Title bar drag?
                if (y >= w->y && y < w->y + TITLEBAR_H) {
                    drag_active = 1;
                    drag_win    = idx;
                    drag_off_x  = x - w->x;
                    drag_off_y  = y - w->y;
                }
                return;
            }
        }
        // Click on desktop — deselect
        if (focused_win >= 0) { windows[focused_win].focused = 0; focused_win = -1; }
    } else {
        // Button released
        drag_active = 0;
        drag_win    = -1;
    }
}
