#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include "isr.h"

void init_keyboard();
char keyboard_get_char(); // Non-blocking character read

// Extended keys (E0-prefixed scancodes) arrive as codes in 0x80-0x8F:
// Latin-1 leaves 0x80-0x9F to C1 controls, so they never collide with
// text, and 0x01-0x1A stay free for Ctrl+A..Ctrl+Z.
#define KEY_UP     0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_PGUP   0x84
#define KEY_PGDN   0x85
#define KEY_HOME   0x86
#define KEY_END    0x87
#define KEY_DEL    0x88

#define CTRL(letter) ((letter) - 'a' + 1)   // CTRL('s') = 0x13

// 1 if `c` is text (printable ASCII or Latin-1), not a control/key code
static inline int key_is_text(char c) {
    uint8_t u = (uint8_t)c;
    return (u >= 0x20 && u < 0x7F) || u >= 0xA0;
}

// Keyboard layouts. Text is Latin-1 (see latin1.h): the ES layout
// produces ñ, ç, accented vowels (dead keys ´ ` ^ ¨) and AltGr symbols.
#define KEYMAP_US 0
#define KEYMAP_ES 1
void keyboard_set_layout(int layout);
int  keyboard_layout();

#endif
