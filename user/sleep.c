// Countdown with SYS_SLEEP and SYS_GETPID: exec disk/sleep.elf 3
// While it sleeps the scheduler skips it, so the system stays idle-fast.
#include "syscall.h"

int main(int argc, char** argv) {
    int secs = 3;
    if (argc > 1 && argv[1][0] >= '1' && argv[1][0] <= '9') secs = argv[1][0] - '0';

    char line[] = "[pid ?] ? ...\n";
    line[5] = '0' + (sys_getpid() % 10);
    for (int i = secs; i > 0; i--) {
        line[8] = '0' + i;
        sys_write(line, sizeof(line) - 1);
        sys_sleep(1000);
    }
    sys_write("Done!\n", 6);
    return 0;
}
