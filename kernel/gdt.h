#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Segment selectors (index << 3 | RPL)
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS   0x1B  // GDT entry 3, RPL 3
#define USER_DS   0x23  // GDT entry 4, RPL 3
#define TSS_SEL   0x28

// Replaces the 2-entry bootloader GDT with one that also has Ring 3
// segments and a TSS. Kernel selectors keep their old values (0x08/0x10),
// so the rest of the kernel is unaffected.
void gdt_init();

// Where the CPU switches the stack when an interrupt arrives from Ring 3
void tss_set_kernel_stack(uint32_t esp0);

#endif
