# 🔥 KaOS

**A custom operating system built from scratch** — by Oskar.

Learn how an OS works from the ground up: bootloader, protected mode,
interrupts, multitasking, user mode, a real filesystem and a graphical
desktop — all written in x86 Assembly and C, with no external dependencies.

## Components

| Component | Name | Description |
|-----------|------|-------------|
| Bootloader | **KaBooM** (Ka Boot Manager) | 16-bit MBR bootloader with visual menu and chainloading |
| Kernel | **KaOS** | 32-bit Protected Mode kernel written in C |
| Userland | `user/` | Ring 3 programs talking to the kernel via `int 0x80` |

## Project Structure

```
boot/           KaBooM bootloader (MBR + partition table)
kernel/         KaOS kernel source (C + ASM)
kernel/commands shell commands (fs, mem, sys, task)
user/           Ring 3 user programs + linker script
docs/           Technical documentation (per subsystem)
build/          Compiled binaries + FAT32 disk image (generated)
build.sh        Build script
kaos.img        Bootable floppy image (generated)
AGENTS.md       Build/run/test guide for contributors and AI agents
```

## Requirements

- **Build**: WSL or Linux with `nasm`, `gcc` (with `gcc-multilib`),
  `dosfstools`, `mtools`
- **Run**: QEMU (or VirtualBox)

## Build

```bash
wsl bash build.sh
```

Produces `kaos.img` (bootable floppy) and `build/fat32.img` (64MB FAT32
hard disk with the user programs — your files on it persist).

## Run

### QEMU (recommended)
```bash
# The floppy attachment matters: the loader uses floppy CHS geometry
qemu-system-i386 -drive format=raw,file=kaos.img,if=floppy \
                 -drive format=raw,file=build/fat32.img,if=ide -boot order=a
```
On Windows, use native QEMU (`winget install SoftwareFreedomConservancy.QEMU`)
rather than QEMU inside WSLg — mouse capture only works natively.
Click inside the window to capture the mouse, `Ctrl+Alt+G` to release.

### VirtualBox
1. Create a VM (Type: Other/Unknown).
2. Add a Floppy Controller and attach `kaos.img`.
3. Optionally attach `build/fat32.img` as a hard disk.
4. Boot order: floppy first.

## Boot Menu (KaBooM)

| Option | Mode |
|--------|------|
| 1. KaOS | Graphical desktop, 800x600x32 (VBE) |
| 2. Recovery Mode | Minimal recovery stub |
| 3. KaOS (Text Mode) | VGA text shell |

## Features

- **KaBooM Boot Menu** — arrow keys + Enter, chainloading from a real
  partition table.
- **32-bit Protected Mode** — GDT with kernel/user segments + TSS.
- **Paging** — kernel pages supervisor-only, dedicated user region.
- **Interrupts (IDT)** — exceptions, PIC remap, hardware IRQs, syscalls.
- **Drivers** — VGA text, VBE graphics, PS/2 keyboard (with Shift),
  PS/2 mouse, PIT timer, CMOS real-time clock, ATA PIO disk (read
  **and write**).
- **Multitasking** — preemptive scheduler (timer-driven) with `ps`, `kill`.
- **User Mode (Ring 3)** — ELF programs run unprivileged; a crashing
  program is killed without taking the system down.
- **System calls** — `int 0x80`: `exit`, `read`, `write`, `open`, `close`,
  `getpid`, `sbrk`, `sleep` (file descriptors over the VFS).
- **Programs** — `main(argc, argv)` with arguments from the shell, exit
  codes reported by the shell, Ctrl+C stops the foreground program.
- **Filesystems** — VFS + RAMFS + FAT32 (read/write, persists across
  reboots), multi-level paths (`cat disk/docs/readme.txt`).
- **Graphical desktop** — double-buffered window manager: dragging,
  focus, minimize, close, taskbar with per-window buttons and a start
  menu, live "About" window, and a full **graphical terminal** running
  the shell.

## Shell Commands

```
help      List available commands        ls        List files (ls disk)
info      Display OS information         cat       Print file (cat disk/hello.txt)
version   Show version numbers           echo      Write file (echo hi > disk/notes.txt)
clear     Clear the screen               malloc    Allocate memory (demo)
halt      Halt the CPU                   free      Free memory (free 0xADDR)
uptime    Show system uptime             paging    Show paging info
date      Show date and time (RTC)
ps        List running tasks             kill      Stop a task by PID
exec      Run an ELF program in Ring 3 (exec disk/args.elf hola mundo)
touch     Create an empty file            rm        Delete a file / empty dir
mkdir     Create a directory (mkdir disk/docs/sub)
spin      Preemption demo (CPU-bound task)
```

## Documentation

Per-subsystem docs live in [docs/](docs/README.md). Development guide for
contributors and AI agents: [AGENTS.md](AGENTS.md). Development roadmap:
[ROADMAP.md](ROADMAP.md).

## License

This project is open source and intended for educational purposes.
