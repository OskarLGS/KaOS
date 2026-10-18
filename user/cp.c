// Copy a file: exec disk/cp.elf readme.txt disk/readme.txt
// Works across filesystems (RAMFS <-> FAT32) since it only uses the VFS
// syscalls. Creates the destination if it does not exist.
#include "syscall.h"

static void say(const char* s) {
    int n = 0;
    while (s[n]) n++;
    sys_write(s, n);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        say("Usage: cp.elf <from> <to>\n");
        return 1;
    }
    int in = sys_open(argv[1], O_RDONLY);
    if (in < 0) { say("cp: cannot open source\n"); return 1; }
    int out = sys_open(argv[2], O_WRONLY | O_CREAT);
    if (out < 0) { say("cp: cannot create destination\n"); sys_close(in); return 1; }

    char buf[512];
    int n, total = 0;
    while ((n = sys_read_fd(in, buf, sizeof(buf))) > 0) {
        if (sys_write_fd(out, buf, n) != n) { say("cp: write failed\n"); return 1; }
        total += n;
    }
    sys_close(in);
    sys_close(out);

    char num[12];
    int i = 11;
    num[i] = '\0';
    do { num[--i] = '0' + total % 10; total /= 10; } while (total);
    say("Copied ");
    say(num + i);
    say(" bytes.\n");
    return 0;
}
