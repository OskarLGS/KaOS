// Prints the arguments it was started with: exec disk/args.elf hola mundo
// Demonstrates main(argc, argv) — crt0 + the stack the kernel lays out.
#include "syscall.h"

static int slen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

int main(int argc, char** argv) {
    char num[2] = { 0, 0 };
    for (int i = 0; i < argc; i++) {
        num[0] = '0' + (i % 10);
        sys_write("argv[", 5);
        sys_write(num, 1);
        sys_write("] = ", 4);
        sys_write(argv[i], slen(argv[i]));
        sys_write("\n", 1);
    }
    return argc - 1; // Exit code = number of real arguments
}
