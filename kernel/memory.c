#include "memory.h"

static memory_block_t* free_list = (memory_block_t*)HEAP_START;
static uint32_t total_used = 0;

void memory_init() {
    free_list->size = HEAP_SIZE - sizeof(memory_block_t);
    free_list->free = 1;
    free_list->next = 0;
    total_used = 0;
}

uint32_t kmalloc(uint32_t size) {
    // Reject absurd sizes: the rounding below (size + 4) and the later
    // (size + header) arithmetic would otherwise wrap around and hand back
    // a tiny block for a huge request — a classic heap-overflow primitive.
    if (size == 0 || size > HEAP_SIZE) return 0;

    // Round size up to a 4-byte boundary
    if (size % 4 != 0) {
        size = (size & ~0x3) + 4;
    }

    memory_block_t* curr = free_list;
    while (curr) {
        if (curr->free && curr->size >= size) {
            // Can we split this block?
            if (curr->size >= size + sizeof(memory_block_t) + 4) {
                memory_block_t* new_block = (memory_block_t*)((uint8_t*)curr + sizeof(memory_block_t) + size);
                new_block->size = curr->size - size - sizeof(memory_block_t);
                new_block->free = 1;
                new_block->next = curr->next;
                
                curr->size = size;
                curr->next = new_block;
            }
            
            curr->free = 0;
            total_used += curr->size + sizeof(memory_block_t);
            return (uint32_t)((uint8_t*)curr + sizeof(memory_block_t));
        }
        curr = curr->next;
    }

    return 0; // Out of memory
}

void kfree(uint32_t addr) {
    // Must point just past a real block header inside the heap
    if (addr < HEAP_START + sizeof(memory_block_t)) return;
    if (addr >= HEAP_START + HEAP_SIZE) return;

    memory_block_t* block = (memory_block_t*)((uint8_t*)addr - sizeof(memory_block_t));
    if (block->free) return;                  // double-free guard
    if (block->size == 0 || block->size > HEAP_SIZE) return; // corrupt header

    block->free = 1;
    total_used -= (block->size + sizeof(memory_block_t));

    // Coalesce free blocks
    memory_block_t* curr = free_list;
    while (curr) {
        if (curr->free && curr->next && curr->next->free) {
            curr->size += curr->next->size + sizeof(memory_block_t);
            curr->next = curr->next->next;
            // Don't move to next yet, check again if the new next can be merged
        } else {
            curr = curr->next;
        }
    }
}

uint32_t get_heap_used() {
    return total_used;
}

uint32_t get_heap_free() {
    return HEAP_SIZE - total_used;
}
