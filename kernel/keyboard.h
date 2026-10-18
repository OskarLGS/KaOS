#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include "isr.h"

void init_keyboard();
char keyboard_get_char(); // Non-blocking character read

// Extended keys (E0-prefixed scancodes) arrive as these control codes,
// which no printable key produces (DC1-DC4, and two more below 0x20).
#define KEY_UP     0x11
#define KEY_DOWN   0x12
#define KEY_LEFT   0x13
#define KEY_RIGHT  0x14
#define KEY_PGUP   0x15
#define KEY_PGDN   0x16

// Keyboard layouts. Text is Latin-1 (see latin1.h): the ES layout
// produces ñ, ç, accented vowels (dead keys ´ ` ^ ¨) and AltGr symbols.
#define KEYMAP_US 0
#define KEYMAP_ES 1
void keyboard_set_layout(int layout);
int  keyboard_layout();

#endif
