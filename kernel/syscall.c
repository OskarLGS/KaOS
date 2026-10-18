#include "syscall.h"
#include "screen.h"
#include "task.h"
#include "paging.h"
#include "keyboard.h"

// A Ring 3 program supplies raw pointers/lengths. Before the kernel
// dereferences them we must confirm the whole range lies inside the user
// region — otherwise a malicious program could make the kernel read its
// own memory (info leak) or fault in kernel context (whole-system hang).
static int user_range_ok(uint32_t addr, uint32_t len) {
    if (len == 0) return 1;
    if (addr < USER_BASE) return 0;
    if (addr >= USER_END) return 0;
    if (len > USER_END - addr) return 0; // also catches addr+len overflow
    return 1;
}

void syscall_handler(struct registers *r) {
    uint32_t syscall_num = r->eax;

    switch (syscall_num) {
        case SYS_EXIT:
            // ebx = exit code
            task_exit((int)r->ebx, EXIT_NORMAL);
            break;

        case SYS_READ: {
            // ebx = fd (0 = stdin), ecx = buffer, edx = max length
            if (r->ebx != 0) { r->eax = (uint32_t)-1; break; }
            if (!user_range_ok(r->ecx, r->edx)) { r->eax = (uint32_t)-1; break; }

            char *buf = (char*)r->ecx;
            uint32_t max = r->edx;
            uint32_t n = 0;

            // Line discipline with local echo (the kernel acts as the tty).
            // Blocks cooperatively via yield() — preemption keeps the rest of
            // the system alive while this task waits for input.
            while (n < max) {
                char c = keyboard_get_char();
                if (c == 0) { yield(); continue; }
                if (c == '\n') { print_string("\n"); break; }
                if (c == '\b') {
                    if (n > 0) { n--; print_string("\b \b"); }
                    continue;
                }
                buf[n++] = c;
                char s[2] = { c, '\0' };
                print_string(s); // echo
            }
            r->eax = n;
            break;
        }

        case SYS_WRITE: {
            // ebx = fd, ecx = buffer, edx = length
            if (r->ebx != 1) {            // only stdout supported
                r->eax = (uint32_t)-1;
                break;
            }
            if (!user_range_ok(r->ecx, r->edx)) {
                r->eax = (uint32_t)-1;    // reject out-of-bounds pointer
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

        default:
            r->eax = (uint32_t)-1;        // unknown syscall
            break;
    }
}
