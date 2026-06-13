# Memory Management — KMalloc & Paging

## KMalloc (Heap Allocator)

### The Heap

The heap is a region of RAM that the kernel can allocate dynamically at runtime. It starts at address `0x100000` (1 MB), safely above conventional memory.

### Bump Allocator

The current allocator is extremely simple — it just moves a pointer forward:

```c
static uint32_t heap_ptr = 0x100000;

uint32_t kmalloc(uint32_t size) {
    size = ALIGN_4(size);     // Align to 4-byte boundary
    uint32_t addr = heap_ptr;
    heap_ptr += size;
    return addr;
}
```

**Pros**: Fast, zero overhead.  
**Cons**: Cannot free memory. Will be replaced with a linked-list allocator in the future.

### Shell Demo

```
> malloc
Allocated 64 bytes at 0x00100000
Total heap used: 64 bytes
> malloc
Allocated 64 bytes at 0x00100040
Total heap used: 128 bytes
```

---

## Paging (Virtual Memory)

### What is Paging?

Paging allows the CPU to translate **virtual addresses** (what the code sees) into **physical addresses** (actual RAM). This is the foundation for process isolation.

### x86 Paging Structure

```
Virtual Address (32 bits)
├── Directory Index (bits 31-22) → selects Page Directory entry
├── Table Index    (bits 21-12) → selects Page Table entry
└── Offset         (bits 11-0)  → offset within the 4KB page
```

- **Page Directory**: 1024 entries, each pointing to a Page Table.
- **Page Table**: 1024 entries, each mapping a 4 KB page.
- **Total coverage per table**: 4 MB (1024 × 4 KB).

### Identity Mapping

We map the first 4 MB so that virtual address = physical address. This means existing code continues to work without changes.

```c
for (int i = 0; i < 1024; i++) {
    first_page_table[i] = (i * 0x1000) | 3; // Present + R/W
}
page_directory[0] = ((uint32_t)first_page_table) | 3;
```

### Enabling Paging

```c
__asm__("mov %0, %%cr3" : : "r"(page_directory)); // Load PD
cr0 |= 0x80000000;                                  // Set PG bit
__asm__("mov %0, %%cr0" : : "r"(cr0));              // Enable
```

### Key Files
- `kernel/memory.c/h` — Heap allocator
- `kernel/paging.c/h` — Page tables and paging setup
