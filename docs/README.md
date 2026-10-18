# 📖 KaOS Technical Documentation

Welcome! This folder contains the technical documentation for KaOS and KaBooM.

Each document covers a specific subsystem of the OS. They are meant to be read independently.

| Document | Topic |
|----------|-------|
| [bootloader.md](bootloader.md) | KaBooM: MBR, relocation, menu, chainloading |
| [protected_mode.md](protected_mode.md) | GDT, 32-bit transition, kernel bridge |
| [interrupts.md](interrupts.md) | IDT, PIC remapping, ISR/IRQ handling |
| [keyboard.md](keyboard.md) | PS/2 keyboard driver, scancode tables |
| [screen.md](screen.md) | VGA text mode, scrolling, cursor |
| [memory.md](memory.md) | KMalloc (heap), paging (virtual memory) |
| [shell.md](shell.md) | Command interpreter, buffer management |
| [ata.md](ata.md) | ATA PIO disk driver, sector read/write |
| [network.md](network.md) | RTL8139, ARP/IPv4/ICMP/UDP/TCP, DHCP, DNS, ping/fetch |
| [rtc.md](rtc.md) | CMOS real-time clock, `date`, file timestamps, Clock app |
| [multitasking.md](multitasking.md) | Task switching, scheduler, context switch |
| [filesystem.md](filesystem.md) | VFS, RAMFS, FAT32 read/write |
| [usermode.md](usermode.md) | Ring 3, GDT/TSS, syscalls, fault containment |
| [graphics.md](graphics.md) | VBE, window manager, taskbar, graphical terminal |
| [build.md](build.md) | Toolchain, build script, disk image layout |

For build/run/test workflows (QEMU flags, headless verification, debug
port trail), see [../AGENTS.md](../AGENTS.md).
