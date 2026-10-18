// Interactive Ring 3 program: asks for your name and greets you.
// Demonstrates SYS_READ (blocking keyboard input) from user mode.
#include "syscall.h"

static int slen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

int main(void) {
    char name[64];

    sys_write("What's your name? ", 18);

    int n = sys_read(name, sizeof(name) - 1);
    name[n] = '\0';

    sys_write("Hello, ", 7);
    sys_write(name, slen(name));
    sys_write("! Welcome to Ring 3.\n", 21);
    return 0;
}
