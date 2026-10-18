// Never ends on its own: shows that Ctrl+C stops the foreground program
// (the shell then reports "^C [interrupted]").
#include "syscall.h"

int main(void) {
    sys_write("Looping forever. Press Ctrl+C to stop me.\n", 42);
    for (;;);
}
