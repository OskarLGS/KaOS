#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

// ─── Physical layout ──────────────────────────────────────────────────────────
// 0 - 16 MB is identity-mapped, supervisor-only, in every address space:
// kernel, heap (1-9 MB) and the frame pool below. Frames for user pages,
// user page tables and page directories come from the pool.
#define FRAME_POOL_START 0x00A00000  // 10 MB
#define FRAME_POOL_END   0x01000000  // 16 MB (1536 frames)
#define PAGE_SIZE        0x1000

// ─── Per-process virtual layout ───────────────────────────────────────────────
// Every process sees its own pages here (programs link at USER_BASE).
#define USER_BASE       0x40000000
#define USER_END        0x40800000   // 8 MB of virtual space per process
#define USER_STACK_TOP  USER_END
#define USER_STACK_SIZE 0x10000      // 64 KB, mapped when the program loads
// The heap (sbrk) may grow up to here: one unmapped guard page below the stack
#define USER_HEAP_LIMIT (USER_STACK_TOP - USER_STACK_SIZE - PAGE_SIZE)

void init_paging(uint32_t fb_addr, uint32_t fb_size);
uint32_t get_page_directory_addr();   // The kernel's page directory

// ─── Frames ───────────────────────────────────────────────────────────────────
uint32_t frame_alloc();               // Zeroed 4 KB frame, or 0 if none left
void     frame_free(uint32_t phys);
uint32_t frames_free();
uint32_t frames_total();

// ─── Address spaces ───────────────────────────────────────────────────────────
// A page directory sharing the kernel's mappings, with no user pages yet.
uint32_t* vmm_create();
// Free every user page, user page table and the directory itself.
void vmm_destroy(uint32_t* pd);
// Map one zeroed, writable user page at `vaddr` (page aligned).
int  vmm_map_user(uint32_t* pd, uint32_t vaddr);
void vmm_unmap_user(uint32_t* pd, uint32_t vaddr);
// 1 if every byte of [addr, addr+len) is in a mapped user page of `pd`.
int  vmm_user_range_ok(uint32_t* pd, uint32_t addr, uint32_t len);
// Copy into a range of `pd`'s user pages (must be mapped) through their
// physical frames — works without switching to that address space.
int  vmm_write(uint32_t* pd, uint32_t vaddr, const void* src, uint32_t len);
// Load `pd` (0 = the kernel's) into CR3.
void vmm_switch(uint32_t* pd);
// Number of user pages mapped in `pd`.
uint32_t vmm_user_pages(uint32_t* pd);

#endif
