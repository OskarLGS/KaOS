// Protection demo: tries to write to kernel memory from Ring 3.
// The page is supervisor-only, so the CPU raises a page fault and the
// kernel kills this task — without taking the system down.
#include "syscall.h"

void _start(void) {
    static const char msg[] = "Attempting to write to kernel memory (0x1000)...\n";
    sys_write(msg, sizeof(msg) - 1);

    *(volatile int*)0x1000 = 0xDEAD; // Kernel page -> page fault

    static const char never[] = "This line should never print.\n";
    sys_write(never, sizeof(never) - 1);
    sys_exit();
    for (;;);
}
