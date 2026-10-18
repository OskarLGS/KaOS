// Exits with the code given as its argument: exec disk/exit.elf 42
// The shell then reports "[exited with code 42]".
#include "syscall.h"

int main(int argc, char** argv) {
    if (argc < 2) return 0;
    int n = 0;
    for (const char* p = argv[1]; *p >= '0' && *p <= '9'; p++)
        n = n * 10 + (*p - '0');
    return n;
}
