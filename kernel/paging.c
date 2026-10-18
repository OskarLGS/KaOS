#include "paging.h"
#include "screen.h"
#include "util.h"

// Page Directory: 1024 entries, each pointing to a Page Table
// Page Table: 1024 entries, each mapping a 4KB page
// Total coverage per Page Table: 4MB (1024 * 4KB)

#define PTE_PRESENT 0x1
#define PTE_WRITE   0x2
#define PTE_USER    0x4

// The kernel's own directory: used by kernel tasks and copied (its kernel
// half) into every process directory. Must be 4KB aligned.
static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t identity_page_tables[4][1024] __attribute__((aligned(4096)));
static uint32_t fb_page_table[1024] __attribute__((aligned(4096)));

#define POOL_FRAMES ((FRAME_POOL_END - FRAME_POOL_START) / PAGE_SIZE)
static uint32_t frame_bitmap[POOL_FRAMES / 32]; // 1 bit per frame, 1 = used
static uint32_t frames_used = 0;

#define USER_PDE_FIRST (USER_BASE >> 22)
#define USER_PDE_LAST  ((USER_END - 1) >> 22)

void init_paging(uint32_t fb_addr, uint32_t fb_size) {
    (void)fb_size;

    // 1. Clear Page Directory
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;
    }

    // 2. Identity-map the first 16 MB (kernel, heap, frame pool), all
    //    supervisor-only. User programs never see physical memory directly:
    //    their pages live in per-process directories (vmm_create).
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 1024; i++) {
            uint32_t addr = t * 0x400000 + i * 0x1000;
            identity_page_tables[t][i] = addr | PTE_PRESENT | PTE_WRITE;
        }
        page_directory[t] = ((uint32_t)identity_page_tables[t]) | PTE_PRESENT | PTE_WRITE;
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

// ─── Frame allocator ──────────────────────────────────────────────────────────
// Bitmap over the pool. Runs with interrupts off: frames are freed by the
// shell while other tasks may be allocating (sbrk) under preemption.

static inline uint32_t lock() {
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags) :: "memory");
    return eflags;
}

static inline void unlock(uint32_t eflags) {
    if (eflags & 0x200) __asm__ __volatile__("sti" ::: "memory");
}

uint32_t frame_alloc() {
    uint32_t f = lock();
    uint32_t phys = 0;
    for (uint32_t w = 0; w < POOL_FRAMES / 32 && !phys; w++) {
        if (frame_bitmap[w] == 0xFFFFFFFF) continue;
        for (int b = 0; b < 32; b++) {
            if (!(frame_bitmap[w] & (1u << b))) {
                frame_bitmap[w] |= 1u << b;
                frames_used++;
                phys = FRAME_POOL_START + (w * 32 + b) * PAGE_SIZE;
                break;
            }
        }
    }
    unlock(f);
    // The pool is identity-mapped for the kernel: clear it in place so a
    // process never sees another one's old data.
    if (phys) memory_set((uint8_t*)phys, 0, PAGE_SIZE);
    return phys;
}

void frame_free(uint32_t phys) {
    if (phys < FRAME_POOL_START || phys >= FRAME_POOL_END || (phys & 0xFFF)) return;
    uint32_t n = (phys - FRAME_POOL_START) / PAGE_SIZE;
    uint32_t f = lock();
    if (frame_bitmap[n / 32] & (1u << (n % 32))) {   // Double-free guard
        frame_bitmap[n / 32] &= ~(1u << (n % 32));
        frames_used--;
    }
    unlock(f);
}

uint32_t frames_free()  { return POOL_FRAMES - frames_used; }
uint32_t frames_total() { return POOL_FRAMES; }

// ─── Address spaces ───────────────────────────────────────────────────────────

uint32_t* vmm_create() {
    uint32_t* pd = (uint32_t*)frame_alloc();
    if (!pd) return 0;
    // Share the kernel half (identity map + framebuffer); the user range
    // starts empty. Kernel entries lack the User bit, so Ring 3 can't
    // touch them.
    for (int i = 0; i < 1024; i++)
        pd[i] = (i >= USER_PDE_FIRST && i <= USER_PDE_LAST) ? 0 : page_directory[i];
    return pd;
}

// Page-table entry for `vaddr` in `pd`, or 0 if its table doesn't exist.
static uint32_t* pte_for(uint32_t* pd, uint32_t vaddr, int create) {
    uint32_t pdi = vaddr >> 22;
    if (pdi < USER_PDE_FIRST || pdi > USER_PDE_LAST) return 0;
    if (!(pd[pdi] & PTE_PRESENT)) {
        if (!create) return 0;
        uint32_t table = frame_alloc();
        if (!table) return 0;
        pd[pdi] = table | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }
    uint32_t* table = (uint32_t*)(pd[pdi] & 0xFFFFF000);
    return &table[(vaddr >> 12) & 0x3FF];
}

static void flush_if_current(uint32_t* pd, uint32_t vaddr) {
    uint32_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    if (cr3 == (uint32_t)pd)
        __asm__ __volatile__("invlpg (%0)" : : "r"(vaddr) : "memory");
}

int vmm_map_user(uint32_t* pd, uint32_t vaddr) {
    if (vaddr < USER_BASE || vaddr >= USER_END || (vaddr & 0xFFF)) return -1;
    uint32_t* pte = pte_for(pd, vaddr, 1);
    if (!pte) return -1;
    if (*pte & PTE_PRESENT) return 0;          // Already mapped
    uint32_t frame = frame_alloc();
    if (!frame) return -1;
    *pte = frame | PTE_PRESENT | PTE_WRITE | PTE_USER;
    flush_if_current(pd, vaddr);
    return 0;
}

void vmm_unmap_user(uint32_t* pd, uint32_t vaddr) {
    uint32_t* pte = pte_for(pd, vaddr, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return;
    frame_free(*pte & 0xFFFFF000);
    *pte = 0;
    flush_if_current(pd, vaddr);
}

void vmm_destroy(uint32_t* pd) {
    if (!pd || pd == page_directory) return;
    for (uint32_t pdi = USER_PDE_FIRST; pdi <= USER_PDE_LAST; pdi++) {
        if (!(pd[pdi] & PTE_PRESENT)) continue;
        uint32_t* table = (uint32_t*)(pd[pdi] & 0xFFFFF000);
        for (int i = 0; i < 1024; i++)
            if (table[i] & PTE_PRESENT) frame_free(table[i] & 0xFFFFF000);
        frame_free((uint32_t)table);
    }
    frame_free((uint32_t)pd);
}

// Physical address backing user `vaddr`, or 0 if not a mapped user page.
static uint32_t translate(uint32_t* pd, uint32_t vaddr) {
    uint32_t* pte = pte_for(pd, vaddr, 0);
    if (!pte) return 0;
    if ((*pte & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) return 0;
    return (*pte & 0xFFFFF000) | (vaddr & 0xFFF);
}

int vmm_user_range_ok(uint32_t* pd, uint32_t addr, uint32_t len) {
    if (len == 0) return 1;
    if (!pd) return 0;
    if (addr < USER_BASE || addr >= USER_END) return 0;
    if (len > USER_END - addr) return 0;       // also catches addr+len overflow
    uint32_t end = addr + len;
    for (uint32_t page = addr & ~0xFFFu; page < end; page += PAGE_SIZE)
        if (!translate(pd, page)) return 0;
    return 1;
}

int vmm_write(uint32_t* pd, uint32_t vaddr, const void* src, uint32_t len) {
    const uint8_t* s = (const uint8_t*)src;
    while (len) {
        uint32_t phys = translate(pd, vaddr);
        if (!phys) return -1;
        uint32_t chunk = PAGE_SIZE - (vaddr & 0xFFF);
        if (chunk > len) chunk = len;
        memory_copy((char*)s, (char*)phys, chunk);
        s     += chunk;
        vaddr += chunk;
        len   -= chunk;
    }
    return 0;
}

void vmm_switch(uint32_t* pd) {
    uint32_t target = pd ? (uint32_t)pd : (uint32_t)page_directory;
    uint32_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    if (cr3 != target)
        __asm__ __volatile__("mov %0, %%cr3" : : "r"(target) : "memory");
}

uint32_t vmm_user_pages(uint32_t* pd) {
    uint32_t n = 0;
    if (!pd) return 0;
    for (uint32_t pdi = USER_PDE_FIRST; pdi <= USER_PDE_LAST; pdi++) {
        if (!(pd[pdi] & PTE_PRESENT)) continue;
        uint32_t* table = (uint32_t*)(pd[pdi] & 0xFFFFF000);
        for (int i = 0; i < 1024; i++)
            if (table[i] & PTE_PRESENT) n++;
    }
    return n;
}
