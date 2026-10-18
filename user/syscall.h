#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

// Minimal userland syscall wrappers (int 0x80, Linux-style numbers)

#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_GETPID  20
#define SYS_SBRK    45
#define SYS_SLEEP   162
#define SYS_CONNECT 300
#define SYS_RESOLVE 301
#define SYS_LISTEN  302
#define SYS_ACCEPT  303

// sys_open flags
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREAT   0x040

#define STDIN  0
#define STDOUT 1
#define STDERR 2

// The "memory" clobber is essential: the kernel reads and writes the
// buffers we pass by pointer. Without it GCC may drop or reorder stores
// to a local buffer before the int 0x80 (it once deleted a whole
// `char line[] = "..."` initialiser, so nothing was printed).
static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
                         : "memory");
    return ret;
}

// Programs normally just return from main (crt0 calls this for them).
static inline void sys_exit(int code) {
    syscall3(SYS_EXIT, code, 0, 0);
}

static inline void sys_write(const char* buf, int len) {
    syscall3(SYS_WRITE, 1 /* stdout */, (int)buf, len);
}

// Reads a line from stdin into buf (up to len bytes). Returns bytes read.
static inline int sys_read(char* buf, int len) {
    return syscall3(SYS_READ, 0 /* stdin */, (int)buf, len);
}

// --- Files: paths like "disk/docs/notes.txt"; fds 0-2 are the console ---

// Returns a file descriptor (3+) or -1.
static inline int sys_open(const char* path, int flags) {
    return syscall3(SYS_OPEN, (int)path, flags, 0);
}

static inline int sys_close(int fd) {
    return syscall3(SYS_CLOSE, fd, 0, 0);
}

// Bytes read (0 = end of file), or -1. fd 0 reads a line from the keyboard.
static inline int sys_read_fd(int fd, void* buf, int len) {
    return syscall3(SYS_READ, fd, (int)buf, len);
}

// Bytes written, or -1. On FAT32 a write sets the file size to its end.
static inline int sys_write_fd(int fd, const void* buf, int len) {
    return syscall3(SYS_WRITE, fd, (int)buf, len);
}

// --- Processes ---

static inline int sys_getpid(void) {
    return syscall3(SYS_GETPID, 0, 0, 0);
}

static inline void sys_sleep(int ms) {
    syscall3(SYS_SLEEP, ms, 0, 0);
}

// Grow (or shrink) the heap; returns the previous break, or (void*)-1.
static inline void* sys_sbrk(int increment) {
    return (void*)syscall3(SYS_SBRK, increment, 0, 0);
}

// --- Network (TCP client): a socket is a file descriptor -------------------

// Resolve a host name (or "a.b.c.d") to an IPv4 address. 0 on success.
static inline int sys_resolve(const char* name, unsigned int* ip) {
    return syscall3(SYS_RESOLVE, (int)name, (int)ip, 0);
}

// Connect to ip:port. Returns an fd for sys_read_fd / sys_write_fd /
// sys_close, -1 if unreachable, -2 if refused.
static inline int sys_connect(unsigned int ip, int port) {
    return syscall3(SYS_CONNECT, (int)ip, port, 0);
}

// Listen for TCP connections on `port` (< 1024: root only). Returns a
// listening fd, -1 on error, -2 if the port is taken.
static inline int sys_listen(int port) {
    return syscall3(SYS_LISTEN, port, 0, 0);
}

// Wait for the next connection; returns its fd (read/write/close).
static inline int sys_accept(int listen_fd) {
    return syscall3(SYS_ACCEPT, listen_fd, 0, 0);
}

#endif
