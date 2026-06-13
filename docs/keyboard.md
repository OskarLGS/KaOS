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

## Input Buffer

Characters are accumulated into a 256-byte `key_buffer[]`. When Enter is pressed (`0x1C`), the buffer is sent to the shell handler and cleared.

Special keys:
- **Backspace** (`0x0E`): removes the last character from the buffer.
- **Enter** (`0x1C`): submits the buffer to the shell.

## Key File
- `kernel/keyboard.c/h`
