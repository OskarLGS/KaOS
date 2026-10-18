# ATA Disk Driver

The ATA driver allows KaOS to read sectors directly from the hard disk using **PIO (Programmed I/O)** mode, without relying on BIOS interrupts.

## ATA PIO Ports (Primary Bus)

| Port | Read | Write |
|------|------|-------|
| `0x1F0` | Data (16-bit) | Data |
| `0x1F1` | Error | Features |
| `0x1F2` | — | Sector count |
| `0x1F3` | — | LBA Low |
| `0x1F4` | — | LBA Mid |
| `0x1F5` | — | LBA High |
| `0x1F6` | — | Drive/Head + LBA top 4 bits |
| `0x1F7` | Status | Command |

## Reading a Sector

1. Wait for BSY (busy) flag to clear.
2. Send drive selection + LBA address to ports `0x1F2`–`0x1F6`.
3. Send READ command (`0x20`) to port `0x1F7`.
4. Wait for BSY to clear again.
5. Wait for DRQ (data ready) flag.
6. Read 256 words (512 bytes) from port `0x1F0`.

## Status Register Bits

| Bit | Name | Meaning |
|-----|------|---------|
| 7   | BSY  | Drive is busy |
| 6   | DRDY | Drive is ready |
| 3   | DRQ  | Data is ready to transfer |
| 0   | ERR  | An error occurred |

## Writing a Sector

1. Wait for BSY to clear, send LBA + drive selection.
2. Send WRITE command (`0x30`).
3. Wait for DRQ, then write 256 words to port `0x1F0`.
4. Wait for BSY to clear, check ERR.

`ata_flush()` sends CACHE FLUSH (`0xE7`) so data reaches the medium.
FAT32 calls it **once at the end of each operation** (write, create,
mkdir, delete, rename), not after every sector.

## Timeouts (important)

With **no drive attached**, the bus floats and the status port reads
`0xFF` — which has BSY set, forever. Every wait loop therefore has a
timeout and an explicit `0xFF` check; `ata_identify()` is called first
so FAT32 can bail out cleanly on disk-less systems. Without this the
kernel hangs at boot.

The limit itself is generous (50 M status reads): a missing drive is
caught by the `0xFF` check at once, while a *slow* one — QEMU keeps BSY
set while the host does the I/O — must not be reported as failed. A
short limit once made FAT reads "fail" mid-write; FAT32 took the error
code for a cluster number and overwrote the FAT. Limits are counted in
status reads, not timer ticks, because syscalls run with interrupts off.

## What's on the ATA disk

KaOS boots from the **floppy** (`kaos.img`, read via BIOS). The ATA
driver serves the separate hard-disk image (`build/fat32.img`), which
holds a FAT32 volume — see [filesystem.md](filesystem.md).

## Key Files
- `kernel/ata.c/h` — ATA PIO read/write driver
- `kernel/ports.c/h` — I/O port wrappers (`inb`, `outb`, `inw`, `outw`)
