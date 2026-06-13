#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

// Ring 3 region: identity-mapped with the User bit. Programs are linked
// at USER_BASE; the user stack grows down from USER_STACK_TOP.
#define USER_BASE      0x00A00000  // 10 MB
#define USER_END       0x01000000  // 16 MB
#define USER_STACK_TOP 0x00F80000  // 15.5 MB

void init_paging(uint32_t fb_addr, uint32_t fb_size);
uint32_t get_page_directory_addr();

#endif
