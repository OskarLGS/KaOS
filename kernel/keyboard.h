#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include "isr.h"

void init_keyboard();
char keyboard_get_char(); // Non-blocking character read

#endif
