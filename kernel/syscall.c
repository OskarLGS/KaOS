#include "syscall.h"
#include "screen.h"
#include "task.h"
#include "paging.h"
#include "keyboard.h"
#include "timer.h"
#include "vfs.h"
#include "util.h"

// A Ring 3 program supplies raw pointers/lengths. Before the kernel
// dereferences them we must confirm the whole range lies in pages the
// process has mapped — otherwise a malicious program could make the kernel
// read its own memory (info leak) or page-fault in kernel context (a
// kernel panic). Syscalls run on the caller's page directory, so once
// checked the pointer can be used directly.
static int user_range_ok(uint32_t addr, uint32_t len) {
    return vmm_user_range_ok(current_task_ptr()->page_dir, addr, len);
}

// Copy a NUL-terminated string from user memory, checking every byte lies
// in the user region. Returns 0, or -1 if it is out of bounds/too long.
static int copy_user_string(char* dst, uint32_t src, uint32_t max) {
    for (uint32_t i = 0; i < max; i++) {
        if (!user_range_ok(src + i, 1)) return -1;
        dst[i] = *(char*)(src + i);
        if (dst[i] == '\0') return 0;
    }
    return -1;
}

// The open file behind `fd`, or 0 (also for 0-2, which are the console).
static file_t* get_file(uint32_t fd) {
    if (fd < 3 || fd >= MAX_FDS) return 0;
    file_t* f = &current_task_ptr()->files[fd];
    return f->node ? f : 0;
}

static int sys_open(uint32_t upath, uint32_t flags) {
    char path[VFS_PATH_MAX];
    if (copy_user_string(path, upath, sizeof(path)) != 0) return -1;

    task_t* t = current_task_ptr();
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++)
        if (!t->files[i].node) { fd = i; break; }
    if (fd < 0) return -1; // Too many open files

    fs_node_t* node = vfs_resolve(path);
    if (!node && (flags & O_CREAT)) {
        char leaf[VFS_PATH_MAX];
        fs_node_t* dir = vfs_resolve_parent(path, leaf);
        if (dir && create_fs(dir, leaf) == 0) node = finddir_fs(dir, leaf);
        vfs_release(dir);
    }
    if (!node) return -1;
    if ((node->flags & 0x7) != FS_FILE) { vfs_release(node); return -1; }

    t->files[fd].node   = node;
    t->files[fd].offset = 0;
    t->files[fd].flags  = flags;
    return fd;
}

static int sys_close(uint32_t fd) {
    file_t* f = get_file(fd);
    if (!f) return -1;
    vfs_release(f->node);
    f->node = 0;
    return 0;
}

#define PAGE_UP(a) (((a) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

// Move the break, mapping or unmapping whole pages as it crosses them.
static int sys_sbrk(int increment) {
    task_t* t = current_task_ptr();
    if (!t->user_brk || !t->page_dir) return -1;
    uint32_t old = t->user_brk;
    uint32_t new_brk = old + (uint32_t)increment;

    // Signed bounds check: catches wrap-around in either direction
    if (increment > 0 && (new_brk < old || new_brk > USER_HEAP_LIMIT)) return -1;
    if (increment < 0 && (new_brk > old || new_brk < t->heap_base)) return -1;

    if (increment > 0) {
        for (uint32_t p = PAGE_UP(old); p < PAGE_UP(new_brk); p += PAGE_SIZE) {
            if (vmm_map_user(t->page_dir, p) != 0) {
                // Out of frames: undo this call's pages and fail
                for (uint32_t q = PAGE_UP(old); q < p; q += PAGE_SIZE)
                    vmm_unmap_user(t->page_dir, q);
                return -1;
            }
        }
        // New pages come zeroed; also clear the tail of the old last page,
        // which may hold data from before an earlier shrink.
        memory_set((uint8_t*)old, 0, PAGE_UP(old) - old);
    } else {
        for (uint32_t p = PAGE_UP(new_brk); p < PAGE_UP(old); p += PAGE_SIZE)
            vmm_unmap_user(t->page_dir, p);
    }
    t->user_brk = new_brk;
    return (int)old;
}

void syscall_handler(struct registers *r) {
    uint32_t syscall_num = r->eax;

    switch (syscall_num) {
        case SYS_EXIT:
            // ebx = exit code
            task_exit((int)r->ebx, EXIT_NORMAL);
            break;

        case SYS_READ: {
            // ebx = fd (0 = stdin, 3+ = open file), ecx = buffer, edx = max length
            if (!user_range_ok(r->ecx, r->edx)) { r->eax = (uint32_t)-1; break; }
            if (r->ebx != 0) {
                file_t* f = get_file(r->ebx);
                if (!f || (f->flags & O_ACCMODE) == O_WRONLY) {
                    r->eax = (uint32_t)-1;
                    break;
                }
                uint32_t n = read_fs(f->node, f->offset, r->edx, (uint8_t*)r->ecx);
                f->offset += n;
                r->eax = n; // 0 = end of file
                break;
            }

            char *buf = (char*)r->ecx;
            uint32_t max = r->edx;
            uint32_t n = 0;

            // Line discipline with local echo (the kernel acts as the tty).
            // Blocks cooperatively via yield() — preemption keeps the rest of
            // the system alive while this task waits for input.
            while (n < max) {
                // Only the foreground program may take keys; a background
                // one waits until the shell hands it the keyboard.
                if (get_foreground() != get_current_task_id()) { yield(); continue; }
                char c = keyboard_get_char();
                if (c == 0) { yield(); continue; }
                if (c == '\n') { print_string("\n"); break; }
                if (c == '\b') {
                    if (n > 0) { n--; print_string("\b \b"); }
                    continue;
                }
                if ((uint8_t)c < 0x20 && c != '\t') continue; // Arrows, PgUp...
                buf[n++] = c;
                char s[2] = { c, '\0' };
                print_string(s); // echo
            }
            r->eax = n;
            break;
        }

        case SYS_WRITE: {
            // ebx = fd (1/2 = console, 3+ = open file), ecx = buffer, edx = length
            if (!user_range_ok(r->ecx, r->edx)) {
                r->eax = (uint32_t)-1;    // reject out-of-bounds pointer
                break;
            }
            if (r->ebx >= 3) {
                file_t* f = get_file(r->ebx);
                if (!f || (f->flags & O_ACCMODE) == O_RDONLY) {
                    r->eax = (uint32_t)-1;
                    break;
                }
                uint32_t n = write_fs(f->node, f->offset, r->edx, (uint8_t*)r->ecx);
                f->offset += n;
                r->eax = n;
                break;
            }
            if (r->ebx != 1 && r->ebx != 2) {
                r->eax = (uint32_t)-1;
                break;
            }

            // Emit in chunks instead of one print_string per byte
            const char *buf = (const char*)r->ecx;
            char chunk[65];
            uint32_t remaining = r->edx;
            while (remaining) {
                uint32_t n = remaining < 64 ? remaining : 64;
                for (uint32_t i = 0; i < n; i++) chunk[i] = *buf++;
                chunk[n] = '\0';
                print_string(chunk);
                remaining -= n;
            }
            r->eax = r->edx;              // bytes written
            break;
        }

        case SYS_OPEN:   // ebx = path, ecx = flags -> fd or -1
            r->eax = (uint32_t)sys_open(r->ebx, r->ecx);
            break;

        case SYS_CLOSE:  // ebx = fd
            r->eax = (uint32_t)sys_close(r->ebx);
            break;

        case SYS_GETPID:
            r->eax = (uint32_t)get_current_task_id();
            break;

        case SYS_SBRK:   // ebx = increment -> previous break or -1
            r->eax = (uint32_t)sys_sbrk((int)r->ebx);
            break;

        case SYS_SLEEP: { // ebx = milliseconds (timer runs at 100 Hz)
            uint32_t ticks = (r->ebx + 9) / 10;
            task_sleep(ticks ? ticks : 1);
            r->eax = 0;
            break;
        }

        default:
            r->eax = (uint32_t)-1;        // unknown syscall
            break;
    }
}
