#include "idt.h"

struct idt_entry idt[256];
struct idt_ptr idt_reg;

void set_idt_gate(int n, uint32_t handler) {
    idt[n].base_low = handler & 0xFFFF;
    idt[n].selector = 0x08; // Code segment in GDT
    idt[n].zero = 0;
    idt[n].flags = 0x8E; // Present, Ring 0, Interrupt Gate
    idt[n].base_high = (handler >> 16) & 0xFFFF;
}

void set_idt() {
    idt_reg.limit = sizeof(struct idt_entry) * 256 - 1;
    idt_reg.base = (uint32_t)&idt;
    __asm__ __volatile__("lidt (%0)" : : "r" (&idt_reg));
}
