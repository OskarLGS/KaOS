#ifndef TASK_H
#define TASK_H

#include <stdint.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 4096

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
    uint8_t stack[TASK_STACK_SIZE]; // Kernel stack (also syscall stack via TSS)
} task_t;

void tasking_init();
int create_task(const char* name, void (*entry)());
int create_user_task(const char* name, uint32_t entry, uint32_t user_esp);

// Exit status of `pid`: returns 1 and fills code/kind once it has stopped,
// 0 while it still runs, -1 if no such task exists (slot recycled).
int task_status(int pid, int* code, exit_kind_t* kind);
// 1 while any Ring 3 program is alive (they all share the user region).
int user_task_running();

// Foreground task = keyboard owner (0 = shell). While a foreground program
// runs, the shell stops consuming keys so the program's SYS_READ gets them.
void set_foreground(int pid);
int  get_foreground();
void schedule();
void yield();
int get_task_count();
task_t* get_task(int index);
int get_current_task_id();
void kexit();                                 // exit(0) for kernel tasks
void task_exit(int code, exit_kind_t kind);   // Stop the current task
void kill(int pid);
void kill_with(int pid, exit_kind_t kind);

// Assembly context switch (defined in task_switch.asm)
extern void switch_context(uint32_t* old_esp, uint32_t new_esp);

#endif
