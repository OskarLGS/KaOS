# Graphical Terminal — Design

**Date:** 2026-06-12
**Status:** Approved (Oskar delegated ordering/design: "continua el desarrollo
en el orden que creas conveniente")

## Goal

Connect the existing shell (`shell.c`, 14 commands, already working in text
mode) to the "Terminal" window of the graphical desktop.

## Problems to solve

1. **Output routing.** Shell commands print via `print_string` (screen.c),
   which in graphical mode draws to *fullscreen* coordinates. Output must go
   into the terminal window instead.
2. **Text rendering in a window.** Needs a character grid with its own
   cursor, wrapping and scrolling, painted relative to the window position.
3. **Input routing.** Keystrokes (already in keyboard.c's ring buffer)
   must reach the terminal only when its window is focused.

## Design

### New module: `kernel/terminal.c` / `terminal.h`

- `TERM_COLS 60 × TERM_ROWS 38` char grid (fits the 500×350 window with
  8×8 font and 6px padding), own cursor (term_cx, term_cy).
- `terminal_init(window_t* win)` — clears grid, sets `win->on_paint`,
  prints banner + `> ` prompt.
- `term_putc(char)` — grid writer: handles `\n`, `\b`, wrap, scroll
  (memmove rows up). This is also the print sink.
- `terminal_paint(window_t*)` — draws grid + underscore cursor at
  `self->x/y` offsets; called by the WM every frame.
- `terminal_handle_key(char)` — line editing identical to the text-mode
  loop in main.c; on Enter runs `shell_handler(line)` with the print sink
  active. `clear` is intercepted and clears the grid (the shell's `clear`
  resets the fullscreen console, which is meaningless here).

### screen.c: output sink

```c
void set_print_sink(void (*sink)(char));
```
When a sink is set, `print_char` forwards to it and returns;
`print_string` skips its `swap_buffers()` (the desktop loop repaints).
Text mode is untouched (sink only set during desktop command execution).

### window.c: focus accessor

`window_t* wm_get_focused(void)` so main.c can check whether the
Terminal window has focus before feeding it keys.

### main.c: wiring

- `terminal_init(terminal_window)` after `wm_create`.
- Desktop loop: poll `keyboard_get_char()`; if the Terminal window is
  focused and visible, `terminal_handle_key(c)` and redraw.

## Out of scope

Multiple terminal instances, ANSI colors, command history, blinking cursor.

## Verification

Scripted QEMU session: boot to desktop, type `help` + Enter, screendump →
command list rendered inside the Terminal window; then `uptime` + Enter.
Confirm About window still updates and text-mode boot is unaffected.
