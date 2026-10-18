#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include "vfs.h"

#define MAX_TASKS 8
#define TASK_STACK_SIZE 4096
#define MAX_FDS 8
#define STACK_CANARY 0xC0FFEE42          // Per task; 0/1/2 are the console (never opened)

typedef enum {
    TASK_RUNNING,
    TASK_READY,
    TASK_STOPPED
} task_state_t;

// How a stopped task ended (reported by the shell after a foreground job)
typedef enum {
    EXIT_NORMAL,      // SYS_EXIT / returned from main: exit_code is valid
    EXIT_FAULT,       // CPU exception in Ring 3: exit_code = exception number
    EXIT_KILLED,      // `kill` command
    EXIT_INTERRUPTED  // Ctrl+C on the foreground program
} exit_kind_t;

// An open file (SYS_OPEN) or socket (SYS_CONNECT). Slots 0-2 stay empty:
// they mean the console. A slot is free when node, sock and listen are 0.
typedef struct {
    fs_node_t* node;     // Open file, or 0
    int sock;            // TCP connection id + 1, or 0
    int listen;          // TCP listener id + 1, or 0
    uint32_t offset;     // Next read/write position (files)
    uint32_t flags;      // O_* flags it was opened with
} file_t;

typedef struct {
    uint32_t esp;        // Saved stack pointer
    uint32_t ebp;        // Saved base pointer
    uint32_t eip;        // Saved instruction pointer
    uint32_t id;         // Task ID
    char name[32];       // Task name
    task_state_t state;  // Current state
    uint32_t user_entry; // Ring 3 entry point (0 = kernel task)
    uint32_t user_esp;   // Initial Ring 3 stack pointer (argc/argv laid out)
    int exit_code;       // Valid once state == TASK_STOPPED
    exit_kind_t exit_kind;
    uint32_t uid;        // Owner (inherited from the creating task)
    int keep_status;     // Zombie: once stopped, keep the slot until
                         // task_status() has handed the exit status over
    uint32_t wake_tick;  // Not scheduled before this timer tick (sleep)
    uint32_t user_brk;   // End of the Ring 3 heap (sbrk); 0 = kernel task
    uint32_t heap_base;  // Initial break: the heap never shrinks below it
    uint32_t* page_dir;  // Own address space (user tasks); 0 = kernel's
    file_t files[MAX_FDS];
    uint8_t stack[TASK_STACK_SIZE]; // Kernel stack (also syscall stack via TSS)
} task_t;

void tasking_init();
int create_task(const char* name, void (*entry)());
// Takes ownership of `page_dir` (freed by task_reap once the task stops).
int create_user_task(const char* name, uint32_t entry, uint32_t user_esp,
                     uint32_t user_brk, uint32_t* page_dir);
// Free the address spaces of stopped tasks. Call from the shell task
// (never from an IRQ): a stopped task may still be running on its own
// page directory until the next switch, which can't happen while the
// shell itself is running.
void task_reap();
task_t* current_task_ptr();
// Block the current task for at least `ticks` timer ticks.
void task_sleep(uint32_t ticks);

// Exit status of `pid`: returns 1 and fills code/kind once it has stopped,
// 0 while it still runs, -1 if no such task exists (slot recycled).
// Returning 1 collects a zombie (see task_keep_status), freeing its slot.
int task_status(int pid, int* code, exit_kind_t* kind);
// Someone will ask for `pid`'s exit status: don't recycle its slot after
// it stops until task_status() has reported it.
void task_keep_status(int pid);
// 1 if `pid` exists and has not stopped
int task_alive(int pid);

// Foreground task = keyboard owner (0 = shell). While a foreground program
// runs, the shell stops consuming keys so the program's SYS_READ gets them.
void set_foreground(int pid);
int  get_foreground();
void schedule();
void yield();
int get_task_count();
task_t* get_task(int index);
int get_current_task_id();
uint32_t current_uid();
void task_set_uid(uint32_t uid);   // Of the current task (the shell, on login)
void kexit();                                 // exit(0) for kernel tasks
void task_exit(int code, exit_kind_t kind);   // Stop the current task
void kill(int pid);
void kill_with(int pid, exit_kind_t kind);

// Assembly context switch (defined in task_switch.asm)
extern void switch_context(uint32_t* old_esp, uint32_t new_esp);

#endif
