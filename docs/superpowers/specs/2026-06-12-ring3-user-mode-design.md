# User Mode (Ring 3) — Design

**Date:** 2026-06-12
**Status:** In progress (Oskar delegated: "continua")

## Goal

Run ELF programs in Ring 3, protected from the kernel: a user program can
print via syscalls, and an attempt to touch kernel memory kills only that
task (not the system).

## Memory layout

```
0x000000 - 0x9FFFFF   Kernel + heap (supervisor pages, ring 3 = fault)
0xA00000 - 0xFFFFFF   User region (U bit set): programs load at 0xA00000,
                      user stack top at 0xF80000
```

One user program at a time (cooperative scheduler; the program runs from
`exec` until SYS_EXIT or a fault). Documented limitation.

## Components

1. **`kernel/gdt.c` + `gdt.h` + `kernel/usermode.asm`** — replace the
   2-entry bootloader GDT with: null, kernel code `0x08`, kernel data
   `0x10`, user code `0x18` (DPL3 → selector `0x1B`), user data `0x20`
   (→ `0x23`), TSS `0x28`. Same kernel selectors as before, so nothing
   else changes. TSS holds `ss0=0x10`, `esp0` = current task's kernel
   stack (set when entering user mode). `gdt_flush`/`tss_flush`/
   `enter_user_mode(entry, ustack)` in the new asm file (added to build.sh).

2. **paging.c** — set the U bit on identity pages covering 10–16 MB.
   Kernel pages keep supervisor-only: that's the actual protection.

3. **isr.c** —
   - IDT gate 0x80 flags `0x8E → 0xEE` (DPL 3) as the old comment planned.
   - `isr_handler`: if the faulting CS was ring 3, print the exception,
     kill the current task and reschedule — instead of halting the OS.

4. **task.c/h** — `create_user_task(name, entry)`: a kernel task whose
   entry is `user_bootstrap()`, which sets `tss.esp0` to its own kernel
   stack top and `enter_user_mode(entry, USER_STACK_TOP)`. New task_t
   fields: `user_entry`. Syscalls from ring 3 arrive on the kernel stack
   via TSS.esp0; SYS_EXIT → `kexit()` → never returns to the program.

5. **elf.c** — load PT_LOAD segments at their real `p_vaddr` (validated
   inside the user region) instead of kmalloc'd addresses, then
   `create_user_task(entry)`.

6. **User programs (`user/`)** — `app.c` (prints via SYS_WRITE, exits)
   and `crash.c` (writes to a kernel address to prove protection), built
   with `user/user.ld` at 0xA00000; build.sh copies them onto
   `build/fat32.img` as APP.ELF / CRASH.ELF (creating the image if absent).

## Verification

Text-mode boot with the FAT32 disk:
1. `exec disk/app.elf` → "Hello from Ring 3!" printed via int 0x80.
2. `exec disk/crash.elf` → page-fault message, task killed, shell still
   alive (`uptime` works afterwards).
