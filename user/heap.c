// User heap demo: grows the heap with SYS_SBRK, fills and checks it.
#include "syscall.h"

int main(void) {
    const int size = 64 * 1024;
    unsigned char* p = sys_sbrk(size);
    if (p == (void*)-1) {
        sys_write("sbrk failed\n", 12);
        return 1;
    }
    for (int i = 0; i < size; i++) p[i] = (unsigned char)i;
    for (int i = 0; i < size; i++) {
        if (p[i] != (unsigned char)i) {
            sys_write("heap corrupted\n", 15);
            return 2;
        }
    }
    sys_write("Allocated and verified 64 KB of user heap.\n", 43);

    // Asking for more than the region holds must fail cleanly
    if (sys_sbrk(16 * 1024 * 1024) == (void*)-1)
        sys_write("Oversized sbrk correctly refused.\n", 34);

    // Grow 1 MB at a time until the kernel runs out of frames (or the
    // region ends): each step really maps pages, and failure is clean.
    int mb = 0;
    while (sys_sbrk(1024 * 1024) != (void*)-1) mb++;
    char msg[] = "Then grew the heap by ? MB more before sbrk said no.\n";
    msg[22] = '0' + (mb % 10);
    sys_write(msg, sizeof(msg) - 1);
    return 0;
}
