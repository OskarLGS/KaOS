#ifndef ISR_H
#define ISR_H

#include <stdint.h>

// Registers structure passed from assembly
struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

void isr_install();
void irq_install();
void isr_handler(struct registers r);
void irq_handler(struct registers r);
void isr128();

typedef void (*irq_handler_t)(struct registers);
void register_interrupt_handler(uint8_t n, irq_handler_t handler);

#endif
