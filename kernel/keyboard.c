#include "keyboard.h"
#include "ports.h"
#include "screen.h"
#include "task.h"

#define SCAN_ENTER 0x1C
#define SCAN_BACKSPACE 0x0E

// volatile: written by the IRQ1 handler, read by the main loop. Without
// it, -O2 caches head/tail in registers in the polling loop and never sees
// the interrupt's updates (keyboard appears dead).
static volatile char ring_buffer[256];
static volatile uint8_t head = 0;
static volatile uint8_t tail = 0;

const char scancode_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
  '9', '0', '-', '=', '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
  '\'', '`',   0,		/* Left shift */
  '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '/',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
};

// Same layout with Shift held (US keyboard)
const char scancode_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*',
  '(', ')', '_', '+', '\b',
  '\t',
  'Q', 'W', 'E', 'R',
  'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,
  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
  '"', '~',   0,
  '|', 'Z', 'X', 'C', 'V', 'B', 'N',
  'M', '<', '>', '?',   0,
  '*',
    0,
  ' ',
    0,
};

#define SCAN_LSHIFT 0x2A
#define SCAN_RSHIFT 0x36
#define SCAN_CTRL   0x1D   // Left Ctrl (right Ctrl is E0-prefixed, same code)
#define SCAN_C      0x2E

static int shift_down = 0;
static int ctrl_down = 0;
static int extended = 0;   // Previous byte was the 0xE0 prefix

// Second byte of E0-prefixed make codes -> KEY_* code (0 = ignore)
static char extended_key(uint8_t scancode) {
    switch (scancode) {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x49: return KEY_PGUP;
        case 0x51: return KEY_PGDN;
        case 0x1C: return '\n';   // Keypad Enter
        default:   return 0;
    }
}

static void keyboard_callback(struct registers* regs) {
    (void)regs;
    uint8_t scancode = port_byte_in(0x60);

    if (scancode == 0xE0) { extended = 1; return; }
    if (extended) {
        extended = 0;
        uint8_t code = scancode & 0x7F;
        if (code == SCAN_CTRL) {                  // Right Ctrl
            ctrl_down = !(scancode & 0x80);
            return;
        }
        if (code == SCAN_LSHIFT || code == SCAN_RSHIFT) return; // Fake shifts
        if (scancode & 0x80) return;              // Extended key release
        char k = extended_key(scancode);
        if (k) ring_buffer[head++] = k;
        return;
    }

    // Track Shift state (bit 7 set = key release)
    if (scancode == SCAN_LSHIFT || scancode == SCAN_RSHIFT) {
        shift_down = 1;
        return;
    }
    if (scancode == (SCAN_LSHIFT | 0x80) || scancode == (SCAN_RSHIFT | 0x80)) {
        shift_down = 0;
        return;
    }

    if (scancode == SCAN_CTRL)          { ctrl_down = 1; return; }
    if (scancode == (SCAN_CTRL | 0x80)) { ctrl_down = 0; return; }

    // Ctrl+C: stop the foreground program. Done here in the IRQ so it
    // works even when the program never reads input (an endless loop).
    if (ctrl_down && scancode == SCAN_C) {
        int fg = get_foreground();
        if (fg) kill_with(fg, EXIT_INTERRUPTED);
        return;
    }

    if (scancode < 0x80) {
        char ascii = 0;
        if (shift_down && scancode < sizeof(scancode_shift)) {
            ascii = scancode_shift[scancode];
        } else if (scancode < sizeof(scancode_ascii)) {
            ascii = scancode_ascii[scancode];
        }
        if (ascii != 0) {
            // Put in ring buffer
            ring_buffer[head++] = ascii;
        }
    }
}

void init_keyboard() {
    register_interrupt_handler(1, keyboard_callback);
}

char keyboard_get_char() {
    if (head == tail) return 0;
    return ring_buffer[tail++];
}
