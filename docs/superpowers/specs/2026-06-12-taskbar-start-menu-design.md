# Taskbar with Start Menu and Window Buttons — Design

**Date:** 2026-06-12
**Status:** Approved by Oskar (option: KaOS button + per-window buttons)

## Goal

Closed windows currently cannot be reopened (close just sets
`visible = 0`; the `window_t` persists). Add a Windows-style taskbar:

- **KaOS start button** (bottom-left, 56px) — toggles a popup menu
  listing every registered window; clicking an item reopens + focuses it.
- **Per-window buttons** — one button per window in the taskbar.
  Click: reopen/focus. Click on the focused window's button: minimize.
  Button color shows state: blue = focused, gray = visible, dark = hidden.

## Design (all in `kernel/window.c`)

### Layout constants
```
TASKBAR_H 28, TASKBAR_Y 572 (matches existing drag clamp)
START_BTN_W 56, TASK_BTN_W 110, TASK_BTN_GAP 4
MENU_W 150, MENU_ITEM_H 24
```

### Drawing
- `wm_draw_desktop()` — extended: taskbar background, start button
  (highlighted while menu open), then one button per window with a
  13-char truncated title.
- `draw_start_menu()` — popup anchored above the taskbar, one row per
  window, drawn **after** windows in `wm_draw_all()` so it sits on top.

### Input (`wm_on_mouse_click`, left press, checked in this order)
1. Menu open and click inside menu → activate item `(y - y0) / MENU_ITEM_H`, close menu.
2. Click in taskbar: start button → toggle menu; window button →
   activate (or minimize if already focused); elsewhere → close menu.
3. Anything else → close menu, fall through to existing window hit test.

`activate_window(i)`: `visible = 1`, transfer focus. Minimize reuses the
close-button refocus logic (topmost remaining visible window).

## Out of scope

Z-order changes (windows still draw in creation order), launching new
app instances (menu lists existing windows only).

## Verification

Scripted QEMU monitor session: close "About KaOS" via its close button,
open start menu, reopen the window from it, then exercise a taskbar
button — screendump after each step and inspect.
