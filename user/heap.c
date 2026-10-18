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
    return 0;
}
