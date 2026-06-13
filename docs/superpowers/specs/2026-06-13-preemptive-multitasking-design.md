# Preemptive Multitasking — Design

**Date:** 2026-06-13
**Status:** Approved (Oskar chose preemptive multitasking)

## Goal

The timer (IRQ0) drives the scheduler, so tasks share the CPU even if they
never call `yield()`. Today the scheduler is cooperative: a task that
spins without yielding freezes the whole system. After this, it doesn't.

## The core idea

`timer_callback` (IRQ0, fires at 100Hz) calls `schedule()`. The existing
`switch_context` primitive is symmetric, so switching from inside an IRQ
works the same as a cooperative switch — the preempted task's full
interrupt frame stays on its kernel stack and is unwound (via `iret`)
when it is resumed.

## What has to change (kernel/task.c, timer.c)

### 1. `timer_callback` → `schedule()`
Remove the "cooperative only" guard. Each tick, try to switch.

### 2. `schedule()` must be interrupt-safe
It is now reachable from **two** contexts (cooperative `yield()` and the
timer IRQ). Without protection, a timer IRQ landing in the middle of a
`yield()` would re-enter `schedule()` and corrupt the task table.

- Save EFLAGS + `cli` at entry; restore at exit (the established
  `pushf; pop; cli ... sti-if-IF-was-set` pattern).
- The timer gate is an interrupt gate (IF cleared on entry), so the IRQ
  path already runs with IF=0 — no nested timer during the switch.
- On resume after `switch_context`, the cooperative caller re-enables
  interrupts; the IRQ caller leaves them off and the later `iret`
  restores IF from the saved frame. Both correct.

### 3. Per-task TSS kernel stack
When switching **to** a task, set `tss.esp0` to that task's kernel-stack
base (`tasks[next].stack + TASK_STACK_SIZE`). This is what the CPU loads
when an interrupt arrives while that task runs in Ring 3. Ring 0 tasks
ignore it, so setting it unconditionally is safe. Replaces the one-shot
set in `user_bootstrap`.

## Why it stays bounded / safe

- Interrupt gate ⇒ no nested timer IRQ inside the handler ⇒ a task gains
  at most one preemption frame at a time before it unwinds (`iret`).
  ~120 bytes/frame, kernel stack is 4096 — comfortable.
- `create_task` already runs under `cli`, so the timer can't preempt a
  half-initialised task.
- `task_count <= 1` ⇒ `schedule()` returns immediately (no overhead, no
  switching when the shell runs alone).

## Demonstration (proof of preemption)

New `spin` shell command (kernel/commands/cmd_task.c):
- First call spawns a kernel task `spinner` running `while(1) spin_count++;`
  **with no yield()**.
- Every call prints the current `spin_count`.

Proof: after `spin`, the shell is still responsive (a non-yielding task no
longer freezes the system), and repeated `spin` shows `spin_count`
climbing while the shell sits idle waiting for input — i.e. the background
task is genuinely getting CPU slices from the timer.

## Verification

Text mode: `spin` → "started"; `ps` lists `spinner`; `spin` again shows a
larger count; `uptime` still answers and keyboard still echoes (pre-change
this would hang). Graphical boot trail unchanged (`GPQVTRKMS!DW`), Ring 3
`exec` + crash-containment still pass.

## Out of scope (next step)

`SYS_READ` / interactive user programs (needs a keyboard-ownership model
between the shell and Ring 3 readers), task reaping (stopped tasks still
hold a slot), priorities. Kept separate to land preemption cleanly.
