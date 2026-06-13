# Multitasking

KaOS supports cooperative multitasking — multiple tasks run concurrently by sharing the CPU through the PIT timer.

## How it works

```
Timer IRQ (100 Hz)
    → timer_callback()
    → schedule()
    → switch_context() [assembly]
        → Save ESP of current task
        → Load ESP of next task
        → Pop restored registers
        → ret (resumes in new task)
```

## Task Structure

Each task has:
- **PID**: Unique integer identifier
- **Name**: Human-readable label
- **State**: RUNNING, READY, or STOPPED
- **Stack**: 4 KB private stack
- **Saved ESP**: Stack pointer at time of last switch

## Round-Robin Scheduling

The scheduler cycles through the task array. On each timer tick:
1. Current task is marked READY.
2. Next non-stopped task is found.
3. That task is marked RUNNING.
4. Context switches to it.

## Context Switch (`task_switch.asm`)

```nasm
switch_context:
    push ebx, esi, edi, ebp     ; Save callee-saved regs
    mov [old_esp], esp           ; Save current stack
    mov esp, new_esp             ; Switch to new stack
    pop ebp, edi, esi, ebx      ; Restore new regs
    ret                          ; "Return" into new task
```

The key insight: when `ret` executes, it pops the return address from the **new** stack — which is the entry point of the task (for new tasks) or the instruction after `switch_context` was called (for resumed tasks).

## Creating a Task

```c
create_task("my_task", my_function);
```

This allocates a 4 KB stack and pushes fake register values so that `switch_context` can "return" into `my_function()`.

## Shell Commands

| Command | Description |
|---------|-------------|
| `ps` | List all tasks with PID, state, and name |

## Key Files
- `kernel/task.c/h` — Task management and scheduler
- `kernel/task_switch.asm` — Assembly context switch
- `kernel/timer.c` — Calls `schedule()` on each tick
