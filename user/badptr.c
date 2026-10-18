// Syscall pointer validation demo: hands the kernel bad pointers and
// shows every call is refused with -1 instead of crashing the system.
#include "syscall.h"

static void report(const char* what, int r) {
    int n = 0;
    while (what[n]) n++;
    sys_write(what, n);
    sys_write(r == -1 ? " -> refused (-1)\n" : " -> ACCEPTED?!\n", r == -1 ? 17 : 15);
}

int main(void) {
    char buf[8];
    report("write from kernel memory 0x1000       ", sys_write_fd(1, (void*)0x1000, 16));
    report("write from unmapped user 0x40400000   ", sys_write_fd(1, (void*)0x40400000, 16));
    report("read into kernel memory 0x100000      ", sys_read_fd(0, (void*)0x100000, 16));
    report("open path in kernel memory 0x1000     ", sys_open((const char*)0x1000, O_RDONLY));
    report("buffer running off the region end     ", sys_write_fd(1, (void*)0x407FFFF8, 64));
    (void)buf;
    return 0;
}
