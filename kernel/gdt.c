#include "gdt.h"
#include "util.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity; // High 4 bits: flags, low 4: limit 19:16
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// 32-bit Task State Segment. We only use ss0/esp0: the stack the CPU
// loads when an interrupt or syscall arrives while running in Ring 3.
struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr   gdt_pointer;
static struct tss_entry tss;

// Defined in usermode.asm
extern void gdt_flush(uint32_t gdt_ptr_addr);
extern void tss_flush();

static void gdt_set(int i, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t gran) {
    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].access      = access;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].base_high   = (base >> 24) & 0xFF;
}

void gdt_init() {
    gdt_pointer.limit = sizeof(gdt) - 1;
    gdt_pointer.base  = (uint32_t)&gdt;

    gdt_set(0, 0, 0, 0, 0);                   // Null
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC0);       // 0x08: kernel code
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC0);       // 0x10: kernel data
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xC0);       // 0x18: user code (DPL 3)
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC0);       // 0x20: user data (DPL 3)

    memory_set((uint8_t*)&tss, 0, sizeof(tss));
    tss.ss0        = KERNEL_DS;
    tss.esp0       = 0; // Set by tss_set_kernel_stack before entering Ring 3
    tss.iomap_base = sizeof(tss);
    gdt_set(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00); // 0x28: TSS

    gdt_flush((uint32_t)&gdt_pointer);
    tss_flush();
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}
