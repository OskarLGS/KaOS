#include "keyboard.h"
#include "ports.h"
#include "screen.h"

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

static int shift_down = 0;

static void keyboard_callback(struct registers regs) {
    (void)regs;
    uint8_t scancode = port_byte_in(0x60);

    // Track Shift state (bit 7 set = key release)
    if (scancode == SCAN_LSHIFT || scancode == SCAN_RSHIFT) {
        shift_down = 1;
        return;
    }
    if (scancode == (SCAN_LSHIFT | 0x80) || scancode == (SCAN_RSHIFT | 0x80)) {
        shift_down = 0;
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
