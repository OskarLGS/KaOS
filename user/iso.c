// Address-space isolation demo. Run several at once:
//   exec disk/iso.elf A &
//   exec disk/iso.elf B
// Each copy stores its letter in the same global — the same virtual
// address in every process — sleeps, and checks nobody overwrote it.
// With a shared user region they would clobber each other.
#include "syscall.h"

static volatile char mark;

int main(int argc, char** argv) {
    mark = (argc > 1) ? argv[1][0] : '?';

    char line[] = "[?] round ? : mark at 0x40000000+ is ? (ok)\n";
    line[1] = mark;
    for (int round = 1; round <= 5; round++) {
        sys_sleep(300);
        line[10] = '0' + round;
        line[37] = mark;
        if (mark != line[1]) {
            sys_write("ISOLATION BROKEN: mark changed!\n", 32);
            return 1;
        }
        sys_write(line, sizeof(line) - 1);
    }
    return 0;
}
