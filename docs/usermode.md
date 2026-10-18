# User Mode (Ring 3)

How KaOS runs unprivileged programs that cannot touch the kernel.

## Memory layout

```
0x000000 - 0x9FFFFF   Kernel, heap, stacks   (supervisor pages)
0xA00000 - 0xFFFFFF   User region            (pages with the U bit)
                      programs linked at 0xA00000 (user/user.ld)
                      user stack top at 0xF80000
```

Defined in `kernel/paging.h` (`USER_BASE`, `USER_END`, `USER_STACK_TOP`).
The protection is the paging U bit: kernel pages don't have it, so any
access from Ring 3 raises a page fault.

## GDT + TSS (kernel/gdt.c, usermode.asm)

`gdt_init()` replaces the 2-entry bootloader GDT:

| Selector | Segment |
|----------|---------|
| 0x08 | Kernel code (unchanged value — nothing else had to change) |
| 0x10 | Kernel data |
| 0x1B | User code, DPL 3 |
| 0x23 | User data, DPL 3 |
| 0x28 | TSS |

The TSS provides `ss0:esp0` — the kernel stack the CPU switches to when
an interrupt or syscall arrives while running in Ring 3.

## Launching a program

`exec disk/app.elf hola mundo` →

1. `cmd_exec` splits the line into words (`argv[0]` = the path).
   `elf_load` (kernel/elf.c) refuses to run while another user program
   is alive (they all link at 0xA00000), copies each PT_LOAD segment to
   its real `p_vaddr` (validated inside the user region), zeroes the
   BSS and lays out `argc`/`argv` at the top of the user stack.
2. `create_user_task` creates a normal kernel task whose entry is
   `user_bootstrap`: it points `tss.esp0` at this task's kernel stack
   and calls `enter_user_mode(entry, user_esp)`.
3. `enter_user_mode` (usermode.asm) loads the user data selectors,
   builds an `iret` frame (`SS=0x23, ESP, EFLAGS|IF, CS=0x1B, EIP`)
   and drops privilege. There is no way back except interrupts.
4. `_start` in `user/crt0.asm` (linked into every program) calls
   `main(argc, argv)` and passes its return value to SYS_EXIT.

Initial user stack (grows down from `USER_STACK_TOP`):

```
USER_STACK_TOP  argument strings ("disk/app.elf\0hola\0mundo\0")
                argv[0..argc-1], NULL      (4-byte aligned)
                argv  (pointer to the array above)
esp ->          argc
```

## Exit status

`task_exit(code, kind)` records how a task ended in `task_t`:

| Kind | Cause | Shell report |
|------|-------|--------------|
| `EXIT_NORMAL` | SYS_EXIT / return from `main` | `[exited with code N]` |
| `EXIT_FAULT` | CPU exception in Ring 3 (code = vector) | `[crashed: Page Fault]` |
| `EXIT_KILLED` | `kill PID` | `[killed]` |
| `EXIT_INTERRUPTED` | Ctrl+C (keyboard IRQ) | `^C [interrupted]` |

`exec` makes the program the foreground job: it owns the keyboard and
the shell withholds its prompt. `shell_poll()`, called from the input
loop, notices when the job stopped, prints the report and the prompt.
`ps` shows stopped tasks as EXITED / CRASHED / KILLED.

## System calls

`int 0x80` (IDT gate flags `0xEE` — DPL 3 so user code may invoke it).
Register convention: `eax` = number, `ebx/ecx/edx` = args.

| eax | Syscall | Args |
|-----|---------|------|
| 1 | SYS_EXIT | ebx=exit code |
| 3 | SYS_READ | ebx=fd (0 = keyboard line, 3+ = file), ecx=buffer, edx=max → bytes, 0 at EOF |
| 4 | SYS_WRITE | ebx=fd (1/2 = console, 3+ = file), ecx=buffer, edx=length |
| 5 | SYS_OPEN | ebx=path, ecx=flags (`O_RDONLY/O_WRONLY/O_RDWR`, `O_CREAT`) → fd |
| 6 | SYS_CLOSE | ebx=fd |
| 20 | SYS_GETPID | → pid |
| 45 | SYS_SBRK | ebx=increment → previous break (sbrk semantics in brk's slot) |
| 162 | SYS_SLEEP | ebx=milliseconds (nanosleep's slot; 10 ms resolution) |

Numbers follow Linux i386 where a call exists; every error returns -1.
Pointers are checked to lie in the user region (`user_range_ok`, and
byte by byte for path strings) before the kernel touches them.

- **Files**: each task has `files[MAX_FDS]` (8; 0-2 are the console and
  never allocated). Paths go through `vfs_resolve`, so they work on
  RAMFS and FAT32 alike (`cp.elf readme.txt disk/docs/copia.txt`).
  Files still open when a task stops (exit, crash, kill, Ctrl+C) are
  closed for it.
- **Heap**: the break starts at the first page after the program image
  and may grow to 64 KB below the stack top. New memory is zeroed.
- **Sleep**: sets `wake_tick`; `schedule()` skips the task until then,
  so a sleeping program costs no CPU.

`user/syscall.h` wraps them (`sys_open`, `sys_read_fd`, `sys_sbrk`, ...).
Its `int 0x80` asm declares a `"memory"` clobber — without it GCC drops
stores to local buffers before the syscall reads them.

Userland wrappers live in `user/syscall.h`. SYS_EXIT calls `task_exit()`,
which marks the task stopped and schedules away — it never returns.

## Fault containment

`isr_handler` checks the saved CS: if the faulting code was Ring 3
(`cs & 3 == 3`), it prints the exception, stops the current task with
`EXIT_FAULT` and keeps the system running. Only kernel (Ring 0) faults halt the machine.
Demo: `exec disk/crash.elf` writes to a kernel address, gets a page
fault, and the shell survives.

## Limitations (by design, for now)

- One user program at a time (fixed load address 0xA00000).
- No per-process address spaces (all user tasks share the
  identity-mapped user region).

## Demo programs (user/)

| Program | Shows |
|---------|-------|
| `app.elf` | SYS_WRITE, clean exit |
| `ask.elf` | SYS_READ (asks your name) |
| `args.elf a b` | `argc`/`argv`; exits with the argument count |
| `exit.elf 42` | Exit codes |
| `loop.elf` | Ctrl+C on a program that never yields |
| `crash.elf` | Fault containment |
| `cat.elf path` | SYS_OPEN / SYS_READ |
| `cp.elf from to` | Copy across RAMFS and FAT32 (O_CREAT, SYS_WRITE to a file) |
| `sleep.elf 3` | SYS_SLEEP + SYS_GETPID countdown |
| `heap.elf` | SYS_SBRK: 64 KB heap, oversized request refused |
