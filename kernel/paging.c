#include "paging.h"
#include "screen.h"

// Page Directory: 1024 entries, each pointing to a Page Table
// Page Table: 1024 entries, each mapping a 4KB page
// Total coverage per Page Table: 4MB (1024 * 4KB)

// Must be 4KB aligned — use static arrays with alignment attribute
static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t identity_page_tables[4][1024] __attribute__((aligned(4096)));
static uint32_t fb_page_table[1024] __attribute__((aligned(4096)));

void init_paging(uint32_t fb_addr, uint32_t fb_size) {
    // 1. Clear Page Directory
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;
    }

    // 2. Identity-map the first 16 MB (Kernel + Heap + Buffer).
    //    Pages from 10 MB up get the User bit: that's where Ring 3
    //    programs and their stacks live (see USER_BASE in paging.h).
    //    Everything below stays supervisor-only — user code touching the
    //    kernel page-faults. The page directory entries need the User bit
    //    too, or the CPU ignores it in the page tables.
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 1024; i++) {
            uint32_t addr = t * 0x400000 + i * 0x1000;
            uint32_t flags = (addr >= USER_BASE) ? 7 : 3; // +User bit
            identity_page_tables[t][i] = addr | flags;
        }
        page_directory[t] = ((uint32_t)identity_page_tables[t]) | 7;
    }

    // 3. Map the Framebuffer
    // Guard: only map if the framebuffer is outside the identity-mapped first 16 MB
    // (pd_index 0-3). Overwriting those entries would destroy the kernel mapping.
    if (fb_addr != 0) {
        uint32_t pd_index = fb_addr >> 22;
        if (pd_index >= 4) {
            page_directory[pd_index] = ((uint32_t)fb_page_table) | 3;

            for (int i = 0; i < 1024; i++) {
                uint32_t page_addr = (fb_addr & 0xFFC00000) + (i * 0x1000);
                fb_page_table[i] = page_addr | 3;
            }
        }
    }

    // 4. Load Page Directory into CR3
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(page_directory));

    // 5. Enable Paging
    uint32_t cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
}

uint32_t get_page_directory_addr() {
    return (uint32_t)page_directory;
}
