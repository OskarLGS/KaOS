#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

void init_mouse();
int  mouse_get_x();
int  mouse_get_y();
uint8_t mouse_get_buttons();

#endif
