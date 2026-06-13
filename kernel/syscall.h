#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "isr.h"

#define SYS_EXIT  1
#define SYS_WRITE 4
#define SYS_READ  3

void syscall_handler(struct registers *r);

#endif
