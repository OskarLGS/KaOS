#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void init_timer(uint32_t frequency);
uint32_t get_tick();
uint32_t get_uptime_seconds();

#endif
