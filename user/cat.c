// User-space cat: exec disk/cat.elf disk/hello.txt
// Reads a file through SYS_OPEN/SYS_READ instead of the shell's builtin.
#include "syscall.h"

static int slen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        sys_write("Usage: cat.elf <path>\n", 22);
        return 1;
    }
    int fd = sys_open(argv[1], O_RDONLY);
    if (fd < 0) {
        sys_write("cat: cannot open ", 17);
        sys_write(argv[1], slen(argv[1]));
        sys_write("\n", 1);
        return 1;
    }

    char buf[128];
    int n;
    while ((n = sys_read_fd(fd, buf, sizeof(buf))) > 0)
        sys_write(buf, n);

    sys_close(fd);
    return 0;
}
