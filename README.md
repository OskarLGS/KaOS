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

## Test

```bash
./test.sh
```

Builds, then boots KaOS seven times in headless QEMU (desktop, files,
programs, users, network, Spanish keyboard, kernel panic) on throw-away
copies of the disk, types commands and checks the output and the disk
contents. ~1.5 minutes.

## Run

### QEMU (recommended)
```bash
# The floppy attachment matters: the loader uses floppy CHS geometry
qemu-system-i386 -drive format=raw,file=kaos.img,if=floppy \
                 -drive format=raw,file=build/fat32.img,if=ide -boot order=a \
                 -netdev user,id=n0 -device rtl8139,netdev=n0
```
The two `-netdev`/`-device` options give KaOS an RTL8139 network card on
QEMU's user-mode network (10.0.2.0/24, gateway 10.0.2.2). Without them
KaOS still boots; networking is just unavailable.
On Windows, use native QEMU (`winget install SoftwareFreedomConservancy.QEMU`)
rather than QEMU inside WSLg — mouse capture only works natively.
Click inside the window to capture the mouse, `Ctrl+Alt+G` to release.

### VirtualBox
1. Create a VM (Type: Other/Unknown).
2. Add a Floppy Controller and attach `kaos.img`.
3. Optionally attach `build/fat32.img` as a hard disk.
4. Boot order: floppy first.

## First boot and login

With a fresh `build/fat32.img` KaOS asks for a **root password** (twice)
and creates `disk/etc`, `disk/home` and `disk/tmp`. After that every
session starts at `KaOS login:`. Add users as root with `useradd <name>`;
each user can write only in `disk/home/<name>` and `disk/tmp`.
See [docs/users.md](docs/users.md).

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
- **Paging** — one address space per process (own page directory),
  frame allocator, kernel pages supervisor-only.
- **Interrupts (IDT)** — exceptions, PIC remap, hardware IRQs, syscalls.
- **Drivers** — VGA text, VBE graphics, PS/2 keyboard (with Shift),
  PS/2 mouse, PIT timer, CMOS real-time clock, ATA PIO disk (read
  **and write**), PCI, RTL8139 network card.
- **Networking** — ARP, IPv4, ICMP, UDP, TCP; DHCP at boot, DNS;
  `ping`, `host`, `fetch` (HTTP).
- **Multitasking** — preemptive scheduler (timer-driven) with `ps`, `kill`.
- **User Mode (Ring 3)** — ELF programs run unprivileged; a crashing
  program is killed without taking the system down.
- **System calls** — `int 0x80`: `exit`, `read`, `write`, `open`, `close`,
  `getpid`, `sbrk`, `sleep`, `connect`, `resolve` (files and TCP
  sockets share one descriptor table).
- **Programs** — `main(argc, argv)` with arguments from the shell, exit
  codes reported by the shell, Ctrl+C stops the foreground program,
  several programs at once (`exec disk/sleep.elf 5 &`).
- **Filesystems** — VFS + RAMFS + FAT32 (read/write, persists across
  reboots), multi-level paths (`cat disk/docs/readme.txt`), long file
  names (LFN), `cp`/`mv`.
- **Graphical desktop** — double-buffered window manager: dragging,
  resizing, focus, minimize, close, taskbar with per-window buttons, a
  clock and a start menu; "About", "Clock" and "Files" (VFS browser)
  windows, and a full **graphical terminal** running the shell (with
  scroll-back on PgUp/PgDn).
- **Shell** — command history with ↑/↓ in text mode and on the desktop.
- **Users** — login, salted SHA-256 passwords, `su`, `passwd`,
  `useradd`, per-process uid and path-based permissions.

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
cp        Copy a file                    mv        Move / rename
spin      Preemption demo (CPU-bound task)
ifconfig  Network settings               ping      Ping a host
host      DNS lookup                     fetch     HTTP GET (fetch 10.0.2.2:8080/f.txt)
arp       ARP cache                      dhcp      Renew the address
whoami    Current user                   users     List accounts
su        Switch user                    passwd    Change password
useradd   Add a user (root)              logout    End the session
keymap    Keyboard layout (keymap es: ñ, tildes, ¿¡, AltGr)
```

## Documentation

Per-subsystem docs live in [docs/](docs/README.md). Development guide for
contributors and AI agents: [AGENTS.md](AGENTS.md). Development roadmap:
[ROADMAP.md](ROADMAP.md).

## License

This project is open source and intended for educational purposes.
