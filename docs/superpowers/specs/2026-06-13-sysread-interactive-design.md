# SYS_READ + Interactive User Programs + Task Reaping — Design

**Date:** 2026-06-13
**Status:** In progress (continuation of Phase 6)

## Goals

1. Ring 3 programs can read keyboard input (`SYS_READ`), enabling
   interactive programs.
2. Stopped tasks free their slot so `exec` can be used repeatedly
   (today every exec leaks a slot; MAX_TASKS=8).

## Task reaping (kernel/task.c)

`create_task` currently always appends at `task_count`. Change it to
first reuse a `TASK_STOPPED` slot in `[1, task_count)` (never slot 0, the
shell); only grow `task_count` if none free. Safe because a stopped task
has already been switched away from (kexit→yield never returns), so its
slot/stack are free to overwrite.

## Keyboard ownership (the foreground model)

In text mode the shell's main loop consumes `keyboard_get_char()`. If a
user program also reads it, keystrokes split randomly. Fix with a single
foreground owner:

- `task.c`: `volatile int foreground_pid` + `set_foreground(pid)` /
  `get_foreground()`. `kexit` clears it if the exiting task owns it.
- `cmd_exec`: after launching, `set_foreground(pid)`.
- `main.c` shell loop: only consume keyboard when `get_foreground() == 0`
  (still `yield()` so the program runs). When the program exits, the
  shell resumes input.

## SYS_READ (kernel/syscall.c, number 3)

```
ebx = fd (0 = stdin), ecx = buffer, edx = max length
```
- Validate `[ecx, ecx+edx)` is in the user region (same as SYS_WRITE).
- Block cooperatively: `while ((c = keyboard_get_char()) == 0) yield();`
  — preemption keeps other tasks alive while we wait.
- Line discipline: read until newline or buffer full; handle backspace;
  echo each char via `print_string` (the kernel acts as the tty).
- Returns the number of bytes read.

## User program (user/ask.c)

Demonstrates the round trip: writes a prompt, `sys_read`s a line, writes
a greeting, exits. Built and copied to fat32.img by build.sh (already
globs `user/*.c`). `user/syscall.h` gains a `sys_read` wrapper.

## Verification

Text mode:
1. `exec disk/ask.elf` → prompt appears; type a name + Enter → greeting
   printed; program exits; shell prompt returns and keyboard works.
2. `exec disk/ask.elf` again several times → still works (reaping; no
   "task table full").
3. Regression: graphical trail `GPQVTRKMS!DW`; `spin` preemption demo;
   `exec disk/crash.elf` still contained.

## Out of scope

Graphical-terminal foreground routing (exec lives in text mode); reading
from files via SYS_READ (stdin only); job control.
