# Graphics, Window Manager & Terminal

How KaOS goes from a VBE framebuffer to a desktop with windows.

## VBE Setup (kernel/kernel.asm)

The 16-bit kernel bridge configures graphics **before** entering
protected mode, using the Bochs VBE extension (supported by QEMU and
VirtualBox) via I/O ports `0x01CE` (index) / `0x01CF` (data):
800x600, 32bpp, linear framebuffer. It stores a small info block for
the C kernel at physical `0x0900`:

| Offset | Field |
|--------|-------|
| 0x0900 | Framebuffer physical address (from VBE mode info) |
| 0x0904 | Width (800) |
| 0x0908 | Height (600) |
| 0x090C | Bits per pixel (32) |
| 0x0910 | Pitch (3200) |
| 0x0914 | `is_graphical` flag (0 in text mode) |

Boot menu option 3 skips all of this and leaves VGA text mode.

## Drawing pipeline (kernel/graphics.c)

- All drawing goes to a **back buffer** in the kernel heap; `swap_buffers()`
  copies it to the framebuffer row by row.
- The mouse cursor is composited **into the back buffer before the copy**
  (saving the pixels underneath, restored after). Drawing it on the
  framebuffer after the copy caused visible flicker.
- Hot paths use x86 string instructions: `fill32` (`rep stosl`) for
  rects/clears, `memory_copy` (`rep movsl`) for the 1.9MB frame copy.
  Per-pixel loops are too slow at -O0 — they caused flicker and mouse
  packet loss.
- Text uses an 8x8 ASCII font (`font.h`, derived from the public-domain
  font8x8), MSB-first rows.

## Window manager (kernel/window.c)

Fixed array of `window_t` (max 8). Each window: position, size, title,
`visible`, `focused`, and an optional `on_paint(window_t*)` callback that
draws the client area each frame.

- **Close** just sets `visible = 0` — the window can be reopened.
- **Taskbar** (bottom 28px): a `KaOS` start button (popup menu listing
  every window; click an item to reopen/focus) plus one button per
  window (blue = focused, gray = visible, dark = hidden). Clicking the
  focused window's button minimizes it.
- Input: `wm_on_mouse_move/click` handle dragging (title bar), focus,
  close button, taskbar and menu — checked in that priority order.

The desktop loop (`run_desktop` in main.c) polls mouse/keyboard, redraws
on any change and once per second (for live stats like the About window).

## Graphical terminal (kernel/terminal.c)

A 60x38 character grid bound to the "Terminal" window:

1. Keystrokes reach it from the desktop loop **only while its window is
   focused** (`wm_get_focused()`).
2. Keys go to the shell's shared line editor (`shell_input_key`: history
   with ↑/↓). The terminal is the permanent **print sink**
   (`set_print_sink` in screen.c): every `print_string` — commands, user
   programs, exception reports — lands in the grid.
3. `terminal_paint` draws the grid relative to the window position. Rows
   scrolling off the top go to a 200-line scroll-back ring; **PgUp/PgDn**
   move the view half a screen (a `[-N]` tag shows how far back), and
   any new output snaps back to the live view.

`clear` clears the grid and the scroll-back (the terminal registers it as
the shell's clear hook).
