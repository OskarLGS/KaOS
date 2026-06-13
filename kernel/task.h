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

typedef struct {
    uint32_t esp;        // Saved stack pointer
    uint32_t ebp;        // Saved base pointer
    uint32_t eip;        // Saved instruction pointer
    uint32_t id;         // Task ID
    char name[32];       // Task name
    task_state_t state;  // Current state
    uint32_t user_entry; // Ring 3 entry point (0 = kernel task)
    uint8_t stack[TASK_STACK_SIZE]; // Kernel stack (also syscall stack via TSS)
} task_t;

void tasking_init();
int create_task(const char* name, void (*entry)());
int create_user_task(const char* name, uint32_t entry);

// Foreground task = keyboard owner (0 = shell). While a foreground program
// runs, the shell stops consuming keys so the program's SYS_READ gets them.
void set_foreground(int pid);
int  get_foreground();
void schedule();
void yield();
int get_task_count();
task_t* get_task(int index);
int get_current_task_id();
void kexit();
void kill(int pid);

// Assembly context switch (defined in task_switch.asm)
extern void switch_context(uint32_t* old_esp, uint32_t new_esp);

#endif
