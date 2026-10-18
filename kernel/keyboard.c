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
#define SCAN_ALT    0x38   // Left Alt; E0 38 is right Alt (AltGr)

static int shift_down = 0;
static int ctrl_down = 0;
static int altgr_down = 0; // Right Alt (E0 38): third symbol on ES keys
static int extended = 0;   // Previous byte was the 0xE0 prefix

// ─── Spanish (ES) layout ──────────────────────────────────────────────────────
// Same physical keys (scan codes) as US, different symbols. Text is
// Latin-1: 0xF1 = ñ, 0xE7 = ç, 0xBA = º, 0xA1 = ¡, ... Codes 1-4 in the
// tables are dead keys: they wait for the next key to make an accented
// letter (´ + a = á).
#define DEAD_GRAVE 1
#define DEAD_ACUTE 2
#define DEAD_CIRC  3
#define DEAD_DIAER 4

static const uint8_t es_normal[0x58] = {
    [0x01] = 27,
    [0x02] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\'', 0xA1, '\b',
    [0x0F] = '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', DEAD_GRAVE, '+', '\n',
    [0x1E] = 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 0xF1, DEAD_ACUTE, 0xBA,
    [0x2B] = 0xE7, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '-',
    [0x37] = '*', [0x39] = ' ', [0x4A] = '-', [0x4E] = '+', [0x56] = '<',
};
static const uint8_t es_shift[0x58] = {
    [0x01] = 27,
    [0x02] = '!', '"', 0xB7, '$', '%', '&', '/', '(', ')', '=', '?', 0xBF, '\b',
    [0x0F] = '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', DEAD_CIRC, '*', '\n',
    [0x1E] = 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0xD1, DEAD_DIAER, 0xAA,
    [0x2B] = 0xC7, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', ';', ':', '_',
    [0x37] = '*', [0x39] = ' ', [0x4A] = '-', [0x4E] = '+', [0x56] = '>',
};
static const uint8_t es_altgr[0x58] = {
    [0x02] = '|', '@', '#', '~', [0x07] = 0xAC,
    [0x1A] = '[', ']', [0x28] = '{', [0x29] = '\\', [0x2B] = '}',
};

static int layout = KEYMAP_US;
static int dead = 0;       // Pending dead key (DEAD_*), 0 = none

void keyboard_set_layout(int l) { layout = l; dead = 0; }
int  keyboard_layout()          { return layout; }

static void push(uint8_t c) { ring_buffer[head++] = (char)c; }

// Apply a pending dead key to `c`: an accented vowel if one exists,
// else the accent itself followed by `c` (space: just the accent).
static void put_after_dead(uint8_t c) {
    static const char vowels[] = "aeiouAEIOU";
    // Latin-1 codes for each vowel with grave/acute/circumflex/diaeresis
    static const uint8_t table[4][10] = {
        { 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9 },
        { 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA },
        { 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB },
        { 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC },
    };
    static const uint8_t mark[5] = { 0, '`', 0xB4, '^', 0xA8 };
    int d = dead;
    dead = 0;
    for (int i = 0; i < 10; i++)
        if (vowels[i] == (char)c) { push(table[d - 1][i]); return; }
    push(mark[d]);
    if (c != ' ') push(c);
}

static void key_es(uint8_t scancode) {
    if (scancode >= sizeof(es_normal)) return;
    uint8_t c = altgr_down ? es_altgr[scancode]
              : shift_down ? es_shift[scancode] : es_normal[scancode];
    if (!c) return;
    if (c <= DEAD_DIAER) {               // A dead key
        if (dead) put_after_dead(' ');   // Two in a row: emit the first
        dead = c;
        return;
    }
    if (dead) put_after_dead(c);
    else push(c);
}

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
        if (code == SCAN_ALT) {                   // Right Alt = AltGr
            altgr_down = !(scancode & 0x80);
            return;
        }
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

    if (scancode < 0x80 && layout == KEYMAP_ES) {
        key_es(scancode);
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
