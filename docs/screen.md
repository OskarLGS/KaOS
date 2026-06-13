# Screen Driver — VGA Text Mode

The screen driver writes directly to Video RAM to display text. No BIOS calls — this is pure 32-bit code.

## VGA Text Mode Memory

Video memory starts at physical address `0xB8000`. The screen is 80 columns × 25 rows. Each character cell is 2 bytes:

```
Byte 0: ASCII character
Byte 1: Attribute (foreground + background color)
```

Total: `80 × 25 × 2 = 4000 bytes`

## Color Attributes

| Bits  | Meaning |
|-------|---------|
| 0-3   | Foreground color |
| 4-6   | Background color |
| 7     | Blink |

Common values:
- `0x0F` = White on Black
- `0x02` = Green on Black
- `0x04` = Red on Black

## Functions

| Function | Description |
|----------|-------------|
| `clear_screen()` | Fills all 4000 bytes with spaces |
| `print_char(c, attr)` | Writes one character at cursor position |
| `print_string(msg)` | Prints a null-terminated string |
| `print_string_at(msg, col, row)` | Prints at specific coordinates |

## Scrolling

When the cursor reaches row 25 (off-screen), the `scroll()` function:
1. Copies each row to the row above it (row 1 → row 0, row 2 → row 1, etc.)
2. Clears the last row (row 24)
3. Resets `cursor_y` to 24

## Cursor Tracking

Two global variables track position:
- `cursor_x` (0-79): Current column
- `cursor_y` (0-24): Current row

Newline (`\n`) resets `cursor_x` to 0 and increments `cursor_y`.

## Key File
- `kernel/screen.c/h`
