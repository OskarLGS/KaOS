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

#endif
