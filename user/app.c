// First KaOS user-mode program: prints via syscalls and exits cleanly.
#include "syscall.h"

int main(void) {
    static const char msg[] =
        "Hello from Ring 3! This program runs in user mode:\n"
        "it cannot touch the kernel and talks to it via int 0x80.\n";
    sys_write(msg, sizeof(msg) - 1);
    return 0;
}
