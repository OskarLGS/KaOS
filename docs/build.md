# Build System

## Requirements

| Tool | Purpose |
|------|---------|
| NASM | Assembler (bootloader + interrupt wrappers) |
| GCC (32-bit) | C compiler (`gcc-multilib`) |
| LD | Linker |
| WSL/Linux | Build environment |
| VirtualBox / QEMU | Testing |

### Install on WSL/Ubuntu

```bash
sudo apt update && sudo apt install build-essential gcc-multilib nasm -y
```

## Build Command

```bash
wsl bash build.sh
```

Output: `kaos.img` (1.44 MB floppy image)

## Disk Image Layout

```
Sector 1       (512 B)     KaBooM bootloader (MBR)
Sector 2       (512 B)     Kernel bridge (16→32 bit)
Sectors 3-17   (7680 B)    KaOS C kernel (linked binary)
Sector 18      (512 B)     Recovery mode
Sectors 19+    (padding)   Zeros (padded to 1.44 MB)
```

## Build Pipeline

```
boot.asm ──[nasm]──→ boot.bin (512 B)

kernel.asm ──[nasm]──→ kernel.bin (512 B)

entry.asm ──[nasm]──→ entry.o ─┐
interrupts.asm ─[nasm]─→ interrupts.o ─┤
screen.c ──[gcc]──→ screen.o ──┤
idt.c ──[gcc]──→ idt.o ────────┤
isr.c ──[gcc]──→ isr.o ────────┼──[ld]──→ full_kernel.bin
keyboard.c ─[gcc]─→ keyboard.o ┤
util.c ──[gcc]──→ util.o ──────┤
shell.c ──[gcc]──→ shell.o ────┤
timer.c ──[gcc]──→ timer.o ────┤
memory.c ──[gcc]──→ memory.o ──┤
paging.c ──[gcc]──→ paging.o ──┤
main.c ──[gcc]──→ main.o ──────┘

recovery.asm ──[nasm]──→ recovery.bin (512 B)

boot.bin + kernel.bin + full_kernel.bin + recovery.bin
    ──[cat]──→ kaos.img (padded to 1.44 MB)
```

## GCC Flags

| Flag | Reason |
|------|--------|
| `-m32` | Generate 32-bit code |
| `-ffreestanding` | No standard library assumptions |
| `-fno-pie` | No position-independent code (we control addresses) |

## Linker Script (`kernel/linker.ld`)

Places all code at virtual address `0x1000`, which matches where the kernel bridge loads it in RAM.

## Key File
- `build.sh`
