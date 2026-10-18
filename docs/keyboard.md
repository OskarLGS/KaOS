# Keyboard Driver

The keyboard driver handles IRQ1 (interrupt 33 after PIC remapping) and converts raw hardware scan codes into ASCII characters.

## How it works

```
Key press → IRQ1 → PIC → CPU → IDT entry 33 → keyboard_callback()
```

The callback reads the scan code from I/O port `0x60`:

```c
uint8_t scancode;
__asm__ __volatile__("inb $0x60, %0" : "=a"(scancode));
```

## Scan Codes vs ASCII

The keyboard sends **scan codes**, not ASCII. Key `A` = scan code `0x1E`. We use a lookup table to translate:

```c
const char scancode_to_ascii[] = {
    0, 27, '1', '2', '3', '4', '5', ...
    'q', 'w', 'e', 'r', 't', 'y', ...
    'a', 's', 'd', 'f', 'g', 'h', ...
};
```

## Key Press vs Key Release

- **Press**: scan code < `0x80`
- **Release**: scan code ≥ `0x80` (press code + `0x80`)

We only process key presses and ignore releases.

## Modifiers and extended keys

- **Shift** (0x2A / 0x36) selects the shifted table; **Ctrl** (0x1D, and
  E0 1D for right Ctrl) is tracked for **Ctrl+C**, which stops the
  foreground program directly from the IRQ.
- `0xE0` prefixes extended keys. The next make code is translated to a
  control code no printable key produces (keyboard.h):

| Key | Scancode | Delivered as |
|-----|----------|--------------|
| ↑ / ↓ | E0 48 / E0 50 | `KEY_UP` 0x11 / `KEY_DOWN` 0x12 |
| ← / → | E0 4B / E0 4D | `KEY_LEFT` 0x13 / `KEY_RIGHT` 0x14 |
| PgUp / PgDn | E0 49 / E0 51 | `KEY_PGUP` 0x15 / `KEY_PGDN` 0x16 |
| Keypad Enter | E0 1C | `'\n'` |

Extended releases and the fake-shift bytes some keys send are dropped.
SYS_READ ignores these codes, so they never end up in a program's line.

## Input Buffer

Keys go into a 256-entry ring buffer (`volatile`: written by the IRQ,
read by `keyboard_get_char()`, which returns 0 when empty). Line editing
(Backspace, Enter, history) is the shell's job — see shell.md.

## Key File
- `kernel/keyboard.c/h`
