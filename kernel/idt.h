#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// IDT Entry structure
struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

// IDT Pointer structure
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern struct idt_entry idt[256];

void set_idt_gate(int n, uint32_t handler);
void set_idt();

#endif
