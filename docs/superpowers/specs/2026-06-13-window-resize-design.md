# Spec: Window Resize by Borders — KaOS Phase 5

**Date:** 2026-06-13  
**Status:** Approved

---

## Overview

Add the ability to resize any window by dragging its borders (right, left, bottom, bottom-right corner, bottom-left corner). The top border is excluded because it hosts the title bar (drag conflict). When a window with an `on_resize` callback is resized, that callback fires on mouse release — the terminal uses this to recalculate its character grid and reflow existing content.

---

## 1. Detection Zones

A **RESIZE_ZONE of 6 px** is centered on each 1 px visual border. Hit test order: corners before sides.

| Zone | Hit condition |
|---|---|
| Right border | `x ∈ [win.x+w−6, win.x+w+6]`, `y > win.y+TITLEBAR_H` |
| Left border | `x ∈ [win.x−6, win.x+6]`, `y > win.y+TITLEBAR_H` |
| Bottom border | `y ∈ [win.y+h−6, win.y+h+6]`, `x ∈ [win.x, win.x+w]` |
| Bottom-right corner | both right + bottom |
| Bottom-left corner | both left + bottom |

Detection runs only when no drag or resize is already active.

---

## 2. Data Structures

### window_t additions (`window.h`)

Three fields are added **after** `on_paint` in the struct:

```c
typedef struct window {
    int      x, y;
    int      w, h;
    char     title[64];
    int      visible;
    int      focused;
    void   (*on_paint)(struct window* self);
    void   (*on_resize)(struct window* self);  // NEW — fired on mouse release after resize
    int      min_w, min_h;                     // NEW — minimum dimensions
} window_t;
```

`wm_create` explicitly sets `win->on_resize = 0; win->min_w = 200; win->min_h = TITLEBAR_H + 60;`.

### Terminal grid sizing (`terminal.c`)

The fixed `#define TERM_COLS 60` and `#define TERM_ROWS 38` are replaced by:

```c
#define MAX_TERM_COLS 98   // (800 - 2*6) / 8
#define MAX_TERM_ROWS 68   // (600 - 28 - 20 - 2*6) / 8  (taskbar + titlebar + padding)

static char term_grid[MAX_TERM_ROWS][MAX_TERM_COLS];
static int  term_cols = 60;   // active width  (initially TERM_COLS)
static int  term_rows = 38;   // active height (initially TERM_ROWS)

static char reflow_buf[MAX_TERM_COLS * MAX_TERM_ROWS + MAX_TERM_ROWS + 1]; // serialize buffer (6733 bytes worst case)
```

Every use of the old `#define` in `terminal.c` is replaced with the `term_cols`/`term_rows` variables. This includes: `term_scroll` (`TERM_ROWS - 1` → `term_rows - 1`, copy length → `(term_rows-1)*term_cols`), `terminal_paint` (loop bounds), `term_putc` (wrap/scroll checks), and `term_clear` (`sizeof` replaced by `term_rows * term_cols`).

### Resize state (`window.c`, file-static)

```c
typedef enum {
    RESIZE_NONE,
    RESIZE_RIGHT, RESIZE_LEFT,
    RESIZE_BOTTOM,
    RESIZE_BR,    // bottom-right corner
    RESIZE_BL     // bottom-left corner
} resize_dir_t;

static resize_dir_t resize_dir   = RESIZE_NONE;
static int          resize_win   = -1;
static int          resize_orig_x, resize_orig_y;
static int          resize_orig_w, resize_orig_h;
static int          resize_start_x, resize_start_y;
```

---

## 3. Mouse Handling

### wm_on_mouse_click (left button pressed)

Priority: close-button → title-bar drag → **resize zone** → focus only.

When a resize zone is hit:
- Save snapshot: `resize_orig_{x,y,w,h}` = current window geometry.
- Save `resize_start_{x,y}` = click position.
- Set `resize_dir` and `resize_win`.
- Return (do not start drag).

### wm_on_mouse_move

When `resize_dir != RESIZE_NONE`:

```
dx = x - resize_start_x
dy = y - resize_start_y

RESIZE_RIGHT:
    w->w = max(orig_w + dx, min_w)

RESIZE_LEFT:   (anchor the right edge)
    new_w = max(orig_w - dx, min_w)
    w->x  = orig_x + orig_w - new_w
    w->w  = new_w

RESIZE_BOTTOM:
    w->h = max(orig_h + dy, min_h)

RESIZE_BR:  apply RIGHT then BOTTOM
RESIZE_BL:  apply LEFT  then BOTTOM
```

Screen clamp applied after each update: `w->x >= 0`, `w->y >= 0`, `w->x + w->w <= 800`, `w->y + w->h <= TASKBAR_Y`.

### wm_on_mouse_click (button released)

```c
if (resize_dir != RESIZE_NONE && resize_win >= 0) {
    window_t* w = &windows[resize_win];
    if (w->visible && w->on_resize) w->on_resize(w);
    resize_dir = RESIZE_NONE;
    resize_win = -1;
}
drag_active = 0;
drag_win    = -1;
```

---

## 4. Visual Indicator

A small **resize grip** (dotted 3×3 pattern, color `0x888888`) is drawn in the bottom-right corner of every window by `draw_window()`, inside the client area:

```c
int gx = w->x + w->w - 10;
int gy = w->y + w->h - 4;
for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
        if ((i + j) % 2 == 0)
            set_pixel(gx + i*3, gy - j*3, 0x888888);
```

No cursor shape change (no hardware cursor sprite system yet).

---

## 5. Terminal Reflow (`terminal.c`)

`terminal_on_resize(window_t* w)` is registered as `win->on_resize` at `terminal_init` time.

```
1. Guard: if (!w->visible) return;

2. Serialize current grid into reflow_buf:
   For each row r in [0, term_rows):
     find last non-space column `last`
     copy term_grid[r][0..last] into buf
     append '\n'
   Null-terminate.

3. Recalculate active dimensions:
   new_cols = clamp((w->w - 2*TERM_PAD) / 8, 10, MAX_TERM_COLS)
   new_rows = clamp((w->h - TITLEBAR_H - 2*TERM_PAD) / 8, 4, MAX_TERM_ROWS)
   term_cols = new_cols
   term_rows = new_rows

4. Clear grid: memset(term_grid, ' ', sizeof(term_grid)); term_cx = 0; term_cy = 0;

5. Re-print reflow_buf via term_putc — wrapping and scrolling are automatic.
```

Known limitation: shrinking the width re-wraps long lines, producing visual artifacts — acceptable for an educational OS.

---

## 6. Files Changed

| File | Change |
|---|---|
| `kernel/window.h` | Add `on_resize`, `min_w`, `min_h` to `window_t` after `on_paint`; add `#define RESIZE_ZONE 6` |
| `kernel/window.c` | Add `resize_dir_t` enum + 6 static vars; update `wm_create`, `wm_on_mouse_click`, `wm_on_mouse_move`; draw grip in `draw_window` |
| `kernel/terminal.h` | Declare `void terminal_on_resize(window_t* w)` |
| `kernel/terminal.c` | Add `MAX_TERM_COLS/ROWS` defines; replace `#define TERM_COLS/ROWS` with `static int term_cols/rows`; add `reflow_buf`; update all loop bounds; implement `terminal_on_resize`; register in `terminal_init` |
| `kernel/main.c` | Set `term_win->min_w = 200; term_win->min_h = TITLEBAR_H + 60;` after `terminal_init` |

---

## 7. Acceptance Criteria

- Dragging the right/left/bottom borders and both bottom corners resizes the window live.
- Window never shrinks below `min_w × min_h`.
- Window never escapes screen bounds or overlaps the taskbar.
- Terminal reflows its content after resize; cursor position is valid (within new bounds).
- Grid writes never exceed `MAX_TERM_COLS × MAX_TERM_ROWS`.
- Existing drag (title bar) behavior is unchanged.
- Close button still works after resize.
