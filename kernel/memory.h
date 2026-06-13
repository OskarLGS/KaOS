#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#define HEAP_START 0x100000  // 1 MB mark
#define HEAP_SIZE  (8 * 1024 * 1024) // 8 MB heap for graphics buffers

typedef struct memory_block {
    uint32_t size;
    uint8_t free;
    struct memory_block* next;
} memory_block_t;

void memory_init();
uint32_t kmalloc(uint32_t size);
void kfree(uint32_t addr);
uint32_t get_heap_used();
uint32_t get_heap_free();

#endif
