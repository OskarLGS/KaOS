#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

// Minimal userland syscall wrappers (int 0x80, Linux-style numbers)

#define SYS_EXIT  1
#define SYS_READ  3
#define SYS_WRITE 4

static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3));
    return ret;
}

static inline void sys_exit() {
    syscall3(SYS_EXIT, 0, 0, 0);
}

static inline void sys_write(const char* buf, int len) {
    syscall3(SYS_WRITE, 1 /* stdout */, (int)buf, len);
}

// Reads a line from stdin into buf (up to len bytes). Returns bytes read.
static inline int sys_read(char* buf, int len) {
    return syscall3(SYS_READ, 0 /* stdin */, (int)buf, len);
}

#endif
