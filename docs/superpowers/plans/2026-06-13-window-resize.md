# Window Resize by Borders — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow the user to resize any KaOS window by dragging its right, left, bottom, bottom-right, or bottom-left borders. The terminal window reflows its character grid on resize.

**Architecture:** The window manager in `window.c` gains a second drag-like state machine (`resize_dir_t`) that detects border hit zones on mouse press, transforms `w/h/x` on mouse move, and fires `win->on_resize` on release. The terminal replaces compile-time grid size constants with runtime variables and implements the callback to serialize → recalculate → reprint its grid.

**Tech Stack:** C (freestanding, -m32, -O2), no stdlib. Build: `./build.sh` from WSL. Run: QEMU (`/mnt/c/Program\ Files/qemu/qemu-system-i386.exe -fda kaos.img`).

---

## File Map

| File | What changes |
|---|---|
| `kernel/window.h` | Add `RESIZE_ZONE` constant; add `on_resize`, `min_w`, `min_h` to `window_t` |
| `kernel/window.c` | Add `resize_dir_t` enum + 6 static state vars; update `wm_create`; add resize zone detection + transformation in `wm_on_mouse_click` / `wm_on_mouse_move`; draw grip in `draw_window` |
| `kernel/terminal.h` | Declare `void terminal_on_resize(window_t* w)` |
| `kernel/terminal.c` | Replace `#define TERM_COLS/ROWS` with `MAX_TERM_COLS/ROWS` + `static int term_cols/rows`; add `reflow_buf`; update all loop bounds; implement `terminal_on_resize`; register in `terminal_init` |
| `kernel/main.c` | Set `term->min_w` / `term->min_h` after `terminal_init` |

---

## Task 1: Extend `window_t` with resize fields

**Files:**
- Modify: `kernel/window.h`
- Modify: `kernel/window.c`

- [ ] **Step 1.1: Add `RESIZE_ZONE` constant and new struct fields**

  Open `kernel/window.h`. After `#define WIN_BORDER 1` add:

  ```c
  #define RESIZE_ZONE     6
  ```

  Inside `typedef struct window`, after `void (*on_paint)(struct window* self);` add:

  ```c
  void   (*on_resize)(struct window* self);
  int      min_w, min_h;
  ```

  The struct should now look like:

  ```c
  typedef struct window {
      int      x, y;
      int      w, h;
      char     title[64];
      int      visible;
      int      focused;
      void   (*on_paint)(struct window* self);
      void   (*on_resize)(struct window* self);
      int      min_w, min_h;
  } window_t;
  ```

- [ ] **Step 1.2: Initialize new fields in `wm_create`**

  In `kernel/window.c`, inside `wm_create`, after `win->on_paint = 0;` add:

  ```c
  win->on_resize = 0;
  win->min_w     = 200;
  win->min_h     = TITLEBAR_H + 60;
  ```

- [ ] **Step 1.3: Build to verify no compile errors**

  ```bash
  ./build.sh 2>&1 | tail -5
  ```

  Expected: `=== Build complete: kaos.img ===`

- [ ] **Step 1.4: Commit**

  ```bash
  git add kernel/window.h kernel/window.c
  git commit -m "feat(wm): add on_resize, min_w, min_h to window_t"
  ```

---

## Task 2: Add resize state machine to `window.c`

**Files:**
- Modify: `kernel/window.c`

- [ ] **Step 2.1: Add `resize_dir_t` enum and static state**

  After the `drag_*` static variables (around line 13), add:

  ```c
  typedef enum {
      RESIZE_NONE,
      RESIZE_RIGHT,
      RESIZE_LEFT,
      RESIZE_BOTTOM,
      RESIZE_BR,
      RESIZE_BL
  } resize_dir_t;

  static resize_dir_t resize_dir    = RESIZE_NONE;
  static int          resize_win    = -1;
  static int          resize_orig_x = 0, resize_orig_y = 0;
  static int          resize_orig_w = 0, resize_orig_h = 0;
  static int          resize_start_x= 0, resize_start_y= 0;
  ```

- [ ] **Step 2.2: Add resize state reset to `wm_init`**

  In `wm_init()`, after `drag_active = 0;` add:

  ```c
  resize_dir = RESIZE_NONE;
  resize_win = -1;
  ```

- [ ] **Step 2.3: Add resize zone detection helper**

  Before `wm_on_mouse_click`, add:

  ```c
  static resize_dir_t detect_resize_zone(window_t* w, int x, int y) {
      int on_right  = (x >= w->x + w->w - RESIZE_ZONE && x <= w->x + w->w + RESIZE_ZONE);
      int on_left   = (x >= w->x - RESIZE_ZONE         && x <= w->x + RESIZE_ZONE);
      int on_bottom = (y >= w->y + w->h - RESIZE_ZONE  && y <= w->y + w->h + RESIZE_ZONE);
      int below_tb  = (y > w->y + TITLEBAR_H);

      if (on_bottom && on_right) return RESIZE_BR;
      if (on_bottom && on_left)  return RESIZE_BL;
      if (on_right  && below_tb) return RESIZE_RIGHT;
      if (on_left   && below_tb) return RESIZE_LEFT;
      if (on_bottom)             return RESIZE_BOTTOM;
      return RESIZE_NONE;
  }
  ```

- [ ] **Step 2.4: Handle resize start in `wm_on_mouse_click` (left button pressed)**

  In the `if (buttons & MOUSE_LEFT)` block, inside the window hit-test loop, after the close-button check and **replacing** the "Title bar drag?" block, use:

  ```c
  // Resize zone? (checked before title-bar drag)
  resize_dir_t rdir = detect_resize_zone(w, x, y);
  if (rdir != RESIZE_NONE) {
      resize_dir     = rdir;
      resize_win     = idx;
      resize_orig_x  = w->x;
      resize_orig_y  = w->y;
      resize_orig_w  = w->w;
      resize_orig_h  = w->h;
      resize_start_x = x;
      resize_start_y = y;
      return;
  }

  // Title bar drag?
  if (y >= w->y && y < w->y + TITLEBAR_H) {
      drag_active = 1;
      drag_win    = idx;
      drag_off_x  = x - w->x;
      drag_off_y  = y - w->y;
  }
  ```

- [ ] **Step 2.5: Handle resize release in `wm_on_mouse_click` (button released)**

  In the `else` branch (button released), replace its entire body with:

  ```c
  if (resize_dir != RESIZE_NONE && resize_win >= 0) {
      window_t* rw = &windows[resize_win];
      if (rw->visible && rw->on_resize) rw->on_resize(rw);
      resize_dir = RESIZE_NONE;
      resize_win = -1;
  }
  drag_active = 0;
  drag_win    = -1;
  ```

- [ ] **Step 2.6: Apply resize transformation in `wm_on_mouse_move`**

  At the very top of `wm_on_mouse_move`, before `cursor_set_pos(x, y);`, add:

  ```c
  if (resize_dir != RESIZE_NONE && resize_win >= 0) {
      window_t* w = &windows[resize_win];
      int dx = x - resize_start_x;
      int dy = y - resize_start_y;

      if (resize_dir == RESIZE_RIGHT || resize_dir == RESIZE_BR) {
          int nw = resize_orig_w + dx;
          if (nw < w->min_w) nw = w->min_w;
          w->w = nw;
      }
      if (resize_dir == RESIZE_LEFT || resize_dir == RESIZE_BL) {
          int nw = resize_orig_w - dx;
          if (nw < w->min_w) nw = w->min_w;
          w->x = resize_orig_x + resize_orig_w - nw;
          w->w = nw;
      }
      if (resize_dir == RESIZE_BOTTOM || resize_dir == RESIZE_BR || resize_dir == RESIZE_BL) {
          int nh = resize_orig_h + dy;
          if (nh < w->min_h) nh = w->min_h;
          w->h = nh;
      }

      // Screen clamp
      if (w->x < 0) w->x = 0;
      if (w->y < 0) w->y = 0;
      if (w->x + w->w > 800)       w->x = 800 - w->w;
      if (w->y + w->h > TASKBAR_Y) w->h = TASKBAR_Y - w->y;
  }
  ```

- [ ] **Step 2.7: Draw resize grip in `draw_window`**

  At the end of `draw_window`, just before `if (w->on_paint) w->on_paint(w);`, add:

  ```c
  // Resize grip — bottom-right corner indicator
  int gx = w->x + w->w - 10;
  int gy = w->y + w->h - 4;
  for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
          if ((i + j) % 2 == 0)
              set_pixel(gx + i * 3, gy - j * 3, 0x888888);
  ```

- [ ] **Step 2.8: Build**

  ```bash
  ./build.sh 2>&1 | tail -5
  ```

  Expected: `=== Build complete: kaos.img ===`

- [ ] **Step 2.9: Commit**

  ```bash
  git add kernel/window.c
  git commit -m "feat(wm): multi-border resize state machine + grip visual"
  ```

---

## Task 3: Dynamic terminal grid + reflow

**Files:**
- Modify: `kernel/terminal.h`
- Modify: `kernel/terminal.c`

- [ ] **Step 3.1: Declare `terminal_on_resize` in the header**

  In `kernel/terminal.h`, after `void terminal_handle_key(char c);` add:

  ```c
  void terminal_on_resize(window_t* w);
  ```

- [ ] **Step 3.2: Replace fixed grid constants with dynamic variables**

  In `kernel/terminal.c`, remove:
  ```c
  #define TERM_COLS 60
  #define TERM_ROWS 38
  ```

  Replace with:
  ```c
  #define MAX_TERM_COLS 98
  #define MAX_TERM_ROWS 68
  #define CHAR_W 8
  #define CHAR_H 8

  static char term_grid[MAX_TERM_ROWS][MAX_TERM_COLS];
  static int  term_cols = 60;
  static int  term_rows = 38;

  static char reflow_buf[MAX_TERM_COLS * MAX_TERM_ROWS + MAX_TERM_ROWS + 1];
  ```

- [ ] **Step 3.3: Update `term_scroll` to use variables**

  Replace the body of `term_scroll`:
  ```c
  static void term_scroll() {
      memory_copy(&term_grid[1][0], &term_grid[0][0],
                  (term_rows - 1) * term_cols);
      memory_set((uint8_t*)&term_grid[term_rows - 1][0], ' ', term_cols);
  }
  ```

- [ ] **Step 3.4: Update `term_putc` to use variables**

  In `term_putc`, replace:
  - `if (term_cx >= TERM_COLS)` → `if (term_cx >= term_cols)`
  - `if (term_cy >= TERM_ROWS)` → `if (term_cy >= term_rows)`

- [ ] **Step 3.5: Update `term_clear` to use variables**

  Replace the body of `term_clear`:
  ```c
  static void term_clear() {
      memory_set((uint8_t*)term_grid, ' ', term_rows * term_cols);
      term_cx = 0;
      term_cy = 0;
  }
  ```

- [ ] **Step 3.6: Update `terminal_paint` to use variables**

  Replace loop bounds:
  - `for (int r = 0; r < TERM_ROWS; r++)` → `for (int r = 0; r < term_rows; r++)`
  - `for (int c = 0; c < TERM_COLS; c++)` → `for (int c = 0; c < term_cols; c++)`

- [ ] **Step 3.7: Implement `terminal_on_resize`**

  After `terminal_paint`, add:

  ```c
  void terminal_on_resize(window_t* w) {
      if (!w || !w->visible) return;

      // 1. Serialize current grid into reflow_buf
      int pos = 0;
      for (int r = 0; r < term_rows; r++) {
          int last = -1;
          for (int c = 0; c < term_cols; c++)
              if (term_grid[r][c] != ' ') last = c;
          for (int c = 0; c <= last && pos < (int)sizeof(reflow_buf) - 2; c++)
              reflow_buf[pos++] = term_grid[r][c];
          if (pos < (int)sizeof(reflow_buf) - 1)
              reflow_buf[pos++] = '\n';
      }
      reflow_buf[pos] = '\0';

      // 2. Recalculate active dimensions
      int nc = (w->w - 2 * TERM_PAD) / CHAR_W;
      int nr = (w->h - TITLEBAR_H - 2 * TERM_PAD) / CHAR_H;
      if (nc < 10) nc = 10;
      if (nr <  4) nr =  4;
      if (nc > MAX_TERM_COLS) nc = MAX_TERM_COLS;
      if (nr > MAX_TERM_ROWS) nr = MAX_TERM_ROWS;
      term_cols = nc;
      term_rows = nr;

      // 3. Clear and reprint
      memory_set((uint8_t*)term_grid, ' ', sizeof(term_grid));
      term_cx = 0;
      term_cy = 0;
      term_print(reflow_buf);
  }
  ```

- [ ] **Step 3.8: Register the callback in `terminal_init`**

  In `terminal_init`, after `if (win) win->on_paint = terminal_paint;` add:

  ```c
  if (win) win->on_resize = terminal_on_resize;
  ```

- [ ] **Step 3.9: Build**

  ```bash
  ./build.sh 2>&1 | tail -5
  ```

  Expected: `=== Build complete: kaos.img ===`

- [ ] **Step 3.10: Commit**

  ```bash
  git add kernel/terminal.h kernel/terminal.c
  git commit -m "feat(terminal): dynamic grid size + reflow on resize"
  ```

---

## Task 4: Wire minimum sizes in `main.c`

**Files:**
- Modify: `kernel/main.c`

- [ ] **Step 4.1: Set min dimensions after `terminal_init`**

  In `run_desktop()`, after `terminal_init(term);` add:

  ```c
  term->min_w = 200;
  term->min_h = TITLEBAR_H + 60;
  ```

- [ ] **Step 4.2: Build**

  ```bash
  ./build.sh 2>&1 | tail -5
  ```

  Expected: `=== Build complete: kaos.img ===`

- [ ] **Step 4.3: Commit**

  ```bash
  git add kernel/main.c
  git commit -m "feat(main): set terminal min size for resize"
  ```

---

## Task 5: Manual verification in QEMU

- [ ] **Step 5.1: Launch QEMU**

  ```bash
  /mnt/c/Program\ Files/qemu/qemu-system-i386.exe -fda kaos.img &
  ```

- [ ] **Step 5.2: Verify right border resize**

  Hover near the right edge of the Terminal window and drag right → window widens. Drag left → narrows. Release → terminal reflows.

- [ ] **Step 5.3: Verify left border resize**

  Drag the left edge left → window widens (left side extends, right stays fixed). Drag right → narrows from the left.

- [ ] **Step 5.4: Verify bottom border resize**

  Drag the bottom edge down → taller. Drag up → shorter.

- [ ] **Step 5.5: Verify corner resize**

  Drag bottom-right → both width and height change. Drag bottom-left → width from left, height from bottom.

- [ ] **Step 5.6: Verify minimum size clamp**

  Try to shrink the Terminal below 200 px wide or below `TITLEBAR_H + 60` px tall — window stops at minimum.

- [ ] **Step 5.7: Verify screen bound clamp**

  Try to resize beyond 800 px wide or past the taskbar — window stops at boundary.

- [ ] **Step 5.8: Verify terminal reflow**

  Type several lines. Resize narrower → text re-wraps. Resize wider → re-wraps again. Cursor is visible and in bounds.

- [ ] **Step 5.9: Verify existing behavior unchanged**

  - Drag title bar → moves (no resize triggered).
  - Click X → minimizes.
  - Click "About KaOS" → comes to front.
  - "About KaOS" window resizes without crash (no `on_resize` callback).

- [ ] **Step 5.10: Final commit**

  ```bash
  git commit -m "feat(phase5): window resize by all borders complete" --allow-empty
  ```
