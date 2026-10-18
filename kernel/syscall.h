#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "isr.h"

// Numbers follow Linux i386 where a matching call exists. SYS_SBRK takes
// the brk slot but has sbrk semantics; SYS_SLEEP takes nanosleep's slot
// but receives milliseconds.
#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_GETPID  20
#define SYS_SBRK    45
#define SYS_SLEEP   162
// KaOS-specific (no Linux equivalent with this shape)
#define SYS_CONNECT 300   // ebx = IPv4 (network order), ecx = port -> fd
#define SYS_RESOLVE 301   // ebx = host name, ecx = &ip -> 0 / -1

// SYS_OPEN flags (Linux values)
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_ACCMODE 0x003
#define O_CREAT   0x040

void syscall_handler(struct registers *r);

#endif
