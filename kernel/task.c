#include "task.h"
#include "util.h"
#include "screen.h"
#include "gdt.h"
#include "paging.h"

// Defined in usermode.asm — builds an iret frame and drops to Ring 3
extern void enter_user_mode(uint32_t entry, uint32_t user_stack_top);

static task_t tasks[MAX_TASKS];
static int task_count = 0;
static int current_task = -1;
static int next_pid = 0;
static int tasking_enabled = 0;

// PID of the task that currently owns keyboard input (0 = the shell). The
// shell stops consuming keys while a foreground program runs, so the
// program's SYS_READ gets them.
static volatile int foreground_pid = 0;

void set_foreground(int pid) { foreground_pid = pid; }
int  get_foreground()        { return foreground_pid; }

static void copy_name(char* dest, const char* src) {
    int i;
    for (i = 0; src[i] && i < 31; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

void tasking_init() {
    copy_name(tasks[0].name, "shell");
    tasks[0].id = next_pid++;
    tasks[0].state = TASK_RUNNING;
    tasks[0].esp = 0; 
    task_count = 1;
    current_task = 0;
    tasking_enabled = 1;
}

int create_task(const char* name, void (*entry)()) {
    // Save IF and disable interrupts while we touch the task table.
    // A bare cli here left the CALLER running with interrupts off forever:
    // its next context save then froze keyboard/timer permanently.
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags));

    // Reuse a stopped slot if one is free (a stopped task has been switched
    // away from for good, so its slot/stack are safe to overwrite). Only
    // grow the table if none can be recycled. Never touch slot 0 (shell).
    int idx = -1;
    for (int i = 1; i < task_count; i++) {
        if (tasks[i].state == TASK_STOPPED) { idx = i; break; }
    }
    if (idx < 0) {
        if (task_count >= MAX_TASKS) {
            if (eflags & 0x200) __asm__ __volatile__("sti");
            return -1;
        }
        idx = task_count++;
    }
    task_t* t = &tasks[idx];

    copy_name(t->name, name);
    t->id = next_pid++;
    t->state = TASK_READY;
    t->user_entry = 0;

    uint32_t* stack_top = (uint32_t*)(t->stack + TASK_STACK_SIZE);

    // switch_context pop order: ebx, esi, edi, ebp, flags, then RET
    *(--stack_top) = (uint32_t)kexit; // Popped by 'ret' after entry() completes
    *(--stack_top) = (uint32_t)entry; // Popped by the FIRST 'ret' in switch_context
    *(--stack_top) = 0x202;           // Popped by popfd (IF=1 set)
    *(--stack_top) = 0;               // EBP
    *(--stack_top) = 0;               // EDI
    *(--stack_top) = 0;               // ESI
    *(--stack_top) = 0;               // EBX

    t->esp = (uint32_t)stack_top;

    if (eflags & 0x200) __asm__ __volatile__("sti");
    return t->id;
}

// Runs as a kernel task: points the TSS at this task's kernel stack (so
// syscalls/interrupts from Ring 3 land on it) and drops to user mode.
static void user_bootstrap() {
    task_t* t = &tasks[current_task];
    tss_set_kernel_stack((uint32_t)(t->stack + TASK_STACK_SIZE));
    enter_user_mode(t->user_entry, USER_STACK_TOP);
}

int create_user_task(const char* name, uint32_t entry) {
    int pid = create_task(name, user_bootstrap);
    if (pid < 0) return pid;

    for (int i = 0; i < task_count; i++) {
        if (tasks[i].id == pid) {
            tasks[i].user_entry = entry;
            break;
        }
    }
    return pid;
}

void schedule() {
    if (!tasking_enabled || task_count <= 1) return;

    // schedule() is reached from BOTH cooperative yield() (IF=1) and the
    // timer IRQ (IF=0). Disable interrupts around the critical section so a
    // timer tick can't re-enter and corrupt the task table mid-switch.
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags));

    int prev = current_task;
    int next = (current_task + 1) % task_count;

    while (next != current_task) {
        if (tasks[next].state == TASK_READY || tasks[next].state == TASK_RUNNING) {
            break;
        }
        next = (next + 1) % task_count;
    }

    if (next != current_task) {
        if (tasks[prev].state == TASK_RUNNING) {
            tasks[prev].state = TASK_READY;
        }
        tasks[next].state = TASK_RUNNING;
        current_task = next;

        // The CPU loads this stack when an interrupt arrives while the next
        // task runs in Ring 3. Ring 0 tasks ignore it, so always setting it
        // is safe.
        tss_set_kernel_stack((uint32_t)(tasks[next].stack + TASK_STACK_SIZE));

        switch_context(&tasks[prev].esp, tasks[next].esp);
    }

    // Cooperative caller: restore IF here. IRQ caller (IF was 0): leave it
    // off — the later iret restores IF from the saved interrupt frame.
    if (eflags & 0x200) __asm__ __volatile__("sti");
}

void yield() {
    schedule();
}

int get_task_count() {
    return task_count;
}

task_t* get_task(int index) {
    if (index < 0 || index >= task_count) return 0;
    return &tasks[index];
}

int get_current_task_id() {
    return (current_task >= 0) ? tasks[current_task].id : -1;
}

void kexit() {
    __asm__ __volatile__("cli");
    if (current_task > 0) { // Never exit the shell (task 0)
        tasks[current_task].state = TASK_STOPPED;
        // Hand keyboard control back to the shell if we owned it
        if (foreground_pid == (int)tasks[current_task].id) foreground_pid = 0;
    }
    yield();
}

void kill(int pid) {
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags));
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].id == pid && i != 0) { // Can't kill the shell
            tasks[i].state = TASK_STOPPED;
            if (foreground_pid == pid) foreground_pid = 0;
            break;
        }
    }
    if (eflags & 0x200) __asm__ __volatile__("sti");
}
