# Protected Mode & Kernel Bridge

The kernel bridge (`kernel/kernel.asm`) transitions the CPU from 16-bit Real Mode to 32-bit Protected Mode.

## Why Protected Mode?

| Feature | Real Mode | Protected Mode |
|---------|-----------|----------------|
| Address space | 1 MB | 4 GB |
| Memory protection | None | Segment-based |
| Interrupts | BIOS (`int`) | IDT-based |

## Steps

### 1. Load the C Kernel from Disk
Using BIOS `int 0x13`, the bridge reads **15 sectors** (starting at sector 3) into memory at `0x1000`.

### 2. Set up the GDT (Global Descriptor Table)
The GDT defines memory segments. We create two flat segments that span all 4 GB:

| Selector | Type | Base | Limit |
|----------|------|------|-------|
| `0x08`   | Code | 0x0  | 4 GB  |
| `0x10`   | Data | 0x0  | 4 GB  |

### 3. Enter Protected Mode
```
cli                    ; Disable interrupts
lgdt [gdt_descriptor]  ; Load GDT
mov eax, cr0
or eax, 0x1            ; Set PE (Protection Enable) bit
mov cr0, eax
jmp 0x08:0x1000        ; Far jump to flush pipeline + load CS
```

### 4. Initialize 32-bit Environment (`kernel/entry.asm`)
The entry point sets up data segment registers (`DS`, `SS`, `ES`, `FS`, `GS`) to selector `0x10` and initializes the stack at `0x90000`.

## Key Files
- `kernel/kernel.asm` — 16-bit bridge
- `kernel/entry.asm` — 32-bit entry point
- `kernel/linker.ld` — Links C code at address `0x1000`
