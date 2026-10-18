#ifndef PANIC_H
#define PANIC_H

#include "isr.h"

// Stop the machine with a readable report: the reason, the CPU state
// (if `r` is given: the exception frame) and the task that was running.
// Drawn straight to the screen (framebuffer or VGA text), since in the
// desktop nothing would repaint the terminal any more, and mirrored to
// the debug port. Never returns.
void panic(const char* reason, struct registers* r) __attribute__((noreturn));

#endif
