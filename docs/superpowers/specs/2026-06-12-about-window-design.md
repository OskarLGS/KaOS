# About KaOS Window Content — Design

**Date:** 2026-06-12
**Status:** Approved by Oskar (option: info + live stats)

## Goal

Fill the empty "About KaOS" window with static system info plus live
uptime and heap usage, using the existing `on_paint` callback that
`window.c` already invokes per frame (`draw_window()` → `w->on_paint(w)`).

## Design

### 1. Paint callback (`kernel/main.c`)

`static void about_paint(window_t* self)` draws into the client area
(origin `self->x`, `self->y + TITLEBAR_H`) with the existing
`draw_string` / `draw_rect` primitives and the 8x8 font:

```
KaOS v1.1.0              (white, headline)
K Operating System       (gray)
──────────────           (1px separator, draw_rect)
Bootloader: KaBooM v1.0
Built by Oskar
Uptime: <N> s            (live, get_uptime_seconds)
Heap: <N> KB used        (live, get_heap_used / 1024)
```

Strings are composed with `strcpy` / `strlen` / `int_to_ascii` from
`util.h` — no new utilities needed.

The callback is assigned right after `wm_create(...)` for the About
window in `run_desktop()`.

### 2. Periodic redraw (`kernel/main.c`)

The desktop loop currently redraws only on mouse activity, so live
stats would go stale. Add a `last_uptime` tracker: when
`get_uptime_seconds()` changes (i.e., once per second), call
`wm_draw_all()`.

## Out of scope

- Terminal window content (separate, larger task).
- Any change to window.c / window.h — the callback infra already exists.

## Verification

Rebuild with `build.sh`, boot in QEMU, screendump after >1s of uptime
and confirm the window shows all 7 lines with non-zero uptime/heap.
