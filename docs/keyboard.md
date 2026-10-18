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
| ↑ / ↓ | E0 48 / E0 50 | `KEY_UP` 0x80 / `KEY_DOWN` 0x81 |
| ← / → | E0 4B / E0 4D | `KEY_LEFT` 0x82 / `KEY_RIGHT` 0x83 |
| PgUp / PgDn | E0 49 / E0 51 | `KEY_PGUP` 0x84 / `KEY_PGDN` 0x85 |
| Home / End / Delete | E0 47 / E0 4F / E0 53 | `KEY_HOME` 0x86 / `KEY_END` 0x87 / `KEY_DEL` 0x88 |
| Keypad Enter | E0 1C | `'\n'` |

Key codes live in 0x80-0x8F because Latin-1 leaves 0x80-0x9F to
control characters, never text. **Ctrl+letter** produces the classic
control code (`CTRL('s')` = 0x13), except Ctrl+C, which the IRQ turns
into "stop the foreground program". `key_is_text(c)` tells text from
these codes.

Extended releases and the fake-shift bytes some keys send are dropped.
SYS_READ ignores these codes, so they never end up in a program's line.

## Layouts: US and ES

`keymap es` / `keymap us` switch layouts (root's choice is saved in
`disk/etc/keymap` and loaded at boot, before the login prompt). The ES
layout maps the same scan codes to Spanish symbols, in **Latin-1**:

| Feature | Details |
|---------|---------|
| Letters | ñ Ñ ç Ç on their own keys |
| Symbols | ¡ ¿ º ª · and `' + - < >` in their Spanish places |
| AltGr (right Alt, `E0 38`) | `\| @ # ~ ¬ [ ] { } \\` |
| Dead keys | `´` `` ` `` `^` `¨`: the next vowel gets the accent (á à â ä); space gives the accent itself; any other key gives accent + key |

## Text encoding (kernel/latin1.c)

Text is one byte per character, Latin-1 (ISO-8859-1). The graphical
font is ASCII; `latin1_glyph()` builds the accented letters by drawing
the accent over the base letter (lowercase: in the two empty top rows;
capitals: moved down one row) plus hand-drawn ¡ ¿ º ª · ¬ ´ ¨. VGA text
mode converts to the card's CP437 (`latin1_to_cp437`, unaccented letter
when CP437 lacks one). FAT32 long names store Latin-1 as the same
Unicode code points, so other systems see `año.txt` correctly; file
*contents* stay Latin-1 bytes.

## Input Buffer

Keys go into a 256-entry ring buffer (`volatile`: written by the IRQ,
read by `keyboard_get_char()`, which returns 0 when empty). Line editing
(Backspace, Enter, history) is the shell's job — see shell.md.

## Key File
- `kernel/keyboard.c/h`
