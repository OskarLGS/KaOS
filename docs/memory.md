# Memory Management — Heap, Frames & Address Spaces

## Physical layout

```
0x00000000 - 0x000FFFFF   Kernel image (at 0x1000), BSS, stack (top 0x90000)
0x00100000 - 0x008FFFFF   Kernel heap (kmalloc), 8 MB
0x00A00000 - 0x00FFFFFF   Frame pool: user pages, user page tables,
                          process page directories (1536 x 4 KB)
framebuffer (VBE)         Mapped at its own physical address
```

All of 0-16 MB is identity-mapped **supervisor-only** in every address
space, so the kernel can reach its heap and every frame by physical
address no matter which process is running.

## Kernel heap (kernel/memory.c)

First-fit free list with block splitting and coalescing (`kmalloc`,
`kfree`). Both run with interrupts off: the heap is shared by every task
and is also reached from IRQ context (Ctrl+C closes a task's files).
`malloc`/`free` shell commands demo it.

## Frame allocator (kernel/paging.c)

A bitmap over the pool (`frame_alloc` / `frame_free`, interrupt-safe).
Frames are zeroed when handed out, so a process never sees another
process's old data. `paging` shows pool usage.

## Address spaces (kernel/paging.c)

Each user program gets its own page directory (`vmm_create`):

| Virtual range | Mapping |
|---------------|---------|
| 0 - 16 MB, framebuffer | Shared kernel entries (same page tables as the kernel's directory), no User bit |
| `0x40000000 - 0x40800000` | This process only: code/data at `USER_BASE`, heap after the image, 64 KB stack under `USER_STACK_TOP` (one unmapped guard page between heap limit and stack) |

| Function | Purpose |
|----------|---------|
| `vmm_map_user` / `vmm_unmap_user` | Map a zeroed frame at a user page / release it |
| `vmm_write` | Copy into another process's pages via their frames (ELF loading, argv) without switching CR3 |
| `vmm_user_range_ok` | Every page of a range mapped with the User bit — syscall pointer validation |
| `vmm_switch` | Load a directory into CR3 (0 = kernel's) |
| `vmm_destroy` | Free all user frames, user page tables and the directory |

`schedule()` loads the next task's directory before switching stacks
(kernel tasks use the kernel's). Kernel stacks live in the shared part,
so the switch is seamless. A stopped program's address space is freed by
`task_reap()` from the shell task — never from an IRQ, because a program
killed by Ctrl+C keeps running on its directory until the next switch.

## Key Files
- `kernel/memory.c/h` — Kernel heap
- `kernel/paging.c/h` — Paging, frame pool, per-process address spaces
