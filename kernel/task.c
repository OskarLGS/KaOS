#include "task.h"
#include "util.h"
#include "screen.h"
#include "gdt.h"
#include "paging.h"
#include "timer.h"

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
        if (tasks[i].state == TASK_STOPPED && !tasks[i].keep_status) { idx = i; break; }
    }
    if (idx < 0) {
        if (task_count >= MAX_TASKS) {
            if (eflags & 0x200) __asm__ __volatile__("sti");
            return -1;
        }
        idx = task_count++;
    }
    task_t* t = &tasks[idx];

    // A recycled slot may still hold a dead program's address space. It
    // stopped before we (the running creator) were scheduled, so it is
    // safe to free.
    if (t->page_dir) vmm_destroy(t->page_dir);

    copy_name(t->name, name);
    t->id = next_pid++;
    t->state = TASK_READY;
    t->user_entry = 0;
    t->user_esp = 0;
    t->exit_code = 0;
    t->exit_kind = EXIT_NORMAL;
    t->keep_status = 0;
    t->wake_tick = 0;
    t->user_brk = 0;
    t->heap_base = 0;
    t->page_dir = 0;
    memory_set((uint8_t*)t->files, 0, sizeof(t->files));

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
    enter_user_mode(t->user_entry, t->user_esp);
}

int create_user_task(const char* name, uint32_t entry, uint32_t user_esp,
                     uint32_t user_brk, uint32_t* page_dir) {
    // Keep interrupts off until the task is fully described: create_task
    // makes it READY, and if the timer ran it before user_entry/page_dir
    // were set, user_bootstrap would jump to address 0.
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags));

    int pid = create_task(name, user_bootstrap);
    for (int i = 0; pid >= 0 && i < task_count; i++) {
        if ((int)tasks[i].id == pid) {
            tasks[i].user_entry = entry;
            tasks[i].user_esp = user_esp;
            tasks[i].user_brk = user_brk;
            tasks[i].heap_base = user_brk;
            tasks[i].page_dir = page_dir;
            break;
        }
    }

    if (eflags & 0x200) __asm__ __volatile__("sti");
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

    // A sleeping task is READY but skipped until its wake tick. The shell
    // (task 0) never sleeps, so there is always something to run.
    uint32_t now = get_tick();
    while (next != current_task) {
        if ((tasks[next].state == TASK_READY || tasks[next].state == TASK_RUNNING)
            && (int32_t)(now - tasks[next].wake_tick) >= 0) {
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

        // Its address space. Kernel stacks and code are mapped identically
        // in every directory, so we keep running fine across the reload.
        vmm_switch(tasks[next].page_dir);

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

task_t* current_task_ptr() {
    return (current_task >= 0) ? &tasks[current_task] : 0;
}

void task_sleep(uint32_t ticks) {
    task_t* t = current_task_ptr();
    if (!t || current_task == 0) return; // The shell must never sleep
    t->wake_tick = get_tick() + ticks;
    // schedule() skips us until wake_tick; loop in case we are picked
    // again early (e.g. we are the only runnable task besides the shell).
    while ((int32_t)(get_tick() - t->wake_tick) < 0) yield();
}

// Close every file a stopped task left open (called with IF=0).
static void close_files(task_t* t) {
    for (int fd = 0; fd < MAX_FDS; fd++) {
        if (t->files[fd].node) {
            vfs_release(t->files[fd].node);
            t->files[fd].node = 0;
        }
    }
}

int get_current_task_id() {
    return (current_task >= 0) ? tasks[current_task].id : -1;
}

int task_status(int pid, int* code, exit_kind_t* kind) {
    for (int i = 0; i < task_count; i++) {
        if ((int)tasks[i].id != pid) continue;
        if (tasks[i].state != TASK_STOPPED) return 0;
        *code = tasks[i].exit_code;
        *kind = tasks[i].exit_kind;
        tasks[i].keep_status = 0;   // Collected: the slot may be reused
        return 1;
    }
    return -1;
}

void task_keep_status(int pid) {
    for (int i = 1; i < task_count; i++)
        if ((int)tasks[i].id == pid) tasks[i].keep_status = 1;
}

void task_reap() {
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags));
    for (int i = 1; i < task_count; i++) {
        if (i == current_task) continue;
        if (tasks[i].state == TASK_STOPPED && tasks[i].page_dir) {
            vmm_destroy(tasks[i].page_dir);
            tasks[i].page_dir = 0;
        }
    }
    if (eflags & 0x200) __asm__ __volatile__("sti");
}

void kexit() {
    task_exit(0, EXIT_NORMAL);
}

void task_exit(int code, exit_kind_t kind) {
    __asm__ __volatile__("cli");
    if (current_task > 0) { // Never exit the shell (task 0)
        tasks[current_task].exit_code = code;
        tasks[current_task].exit_kind = kind;
        tasks[current_task].state = TASK_STOPPED;
        close_files(&tasks[current_task]);
        // Hand keyboard control back to the shell if we owned it
        if (foreground_pid == (int)tasks[current_task].id) foreground_pid = 0;
    }
    yield();
}

void kill(int pid) {
    kill_with(pid, EXIT_KILLED);
}

// Safe from IRQ context (Ctrl+C): only flips state; the scheduler never
// picks a stopped task again, so it is gone by the next switch.
void kill_with(int pid, exit_kind_t kind) {
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags));
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].id == pid && i != 0) { // Can't kill the shell
            if (tasks[i].state == TASK_STOPPED) break; // Keep its real status
            tasks[i].exit_code = -1;
            tasks[i].exit_kind = kind;
            tasks[i].state = TASK_STOPPED;
            close_files(&tasks[i]);
            if (foreground_pid == pid) foreground_pid = 0;
            break;
        }
    }
    if (eflags & 0x200) __asm__ __volatile__("sti");
}
