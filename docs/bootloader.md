# KaBooM — The Bootloader

KaBooM (Ka Boot Manager) is the first code that runs when the computer starts. It lives in the **Master Boot Record (MBR)**, the very first 512 bytes of the disk.

## What it does

1. **Self-relocation**: The BIOS loads the MBR at `0x7C00`. KaBooM copies itself to `0x0600` to free up `0x7C00` for the OS it will load.
2. **Menu**: Displays a text menu with arrow-key navigation. The user selects between KaOS and Recovery Mode.
3. **Chainloading**: Reads the selected OS's boot sector from disk (using BIOS interrupt `int 0x13`) and jumps to it at `0x7C00`.

## Memory Layout (16-bit Real Mode)

```
0x0000 - 0x05FF   Reserved (IVT, BIOS data)
0x0600 - 0x07FF   KaBooM (relocated here)
0x7C00 - 0x7DFF   Loaded OS boot sector
```

## MBR Partition Table

KaBooM includes a simulated MBR partition table at offset 446. Each entry (16 bytes) contains the starting sector of a bootable partition.

| Entry | Bootable | Sector | Description |
|-------|----------|--------|-------------|
| 1     | Yes (0x80) | 2    | KaOS kernel bridge |
| 2     | No (0x00)  | 18   | Recovery mode |

## Key Source: `boot/boot.asm`

- **Input**: Arrow keys (`0x48`/`0x50`) and Enter (`0x0D`) via `int 0x16`.
- **Output**: Text via `int 0x10` (teletype mode `0x0E`).
- **Drive ID**: Saved from `DL` register (provided by BIOS at boot).

## Boot Signature

The last two bytes of the MBR must be `0xAA55` — the "magic number" that tells the BIOS this sector is bootable.
