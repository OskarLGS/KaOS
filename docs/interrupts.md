# Interrupts — IDT, PIC, ISR/IRQ

Interrupts are the nervous system of the OS. They allow the CPU to react to events (keyboard press, timer tick, errors) without constantly polling.

## Architecture

```
Hardware event (e.g. key press)
    → PIC translates to IRQ number
    → CPU looks up IDT entry
    → Jumps to ISR (assembly wrapper)
    → Calls C handler function
    → Sends EOI (End of Interrupt) to PIC
    → Returns to interrupted code (iret)
```

## IDT (Interrupt Descriptor Table)

A table of 256 entries. Each entry (8 bytes) contains:
- **Handler address** (split into low/high 16-bit halves)
- **Selector** (`0x08` — our code segment)
- **Flags** (`0x8E` — present, ring 0, 32-bit interrupt gate)

Loaded into the CPU with the `lidt` instruction.

## PIC Remapping

The 8259 PIC maps hardware IRQs to interrupt numbers. By default, IRQ0-7 map to interrupts 8-15, which **conflict** with CPU exceptions (e.g. Double Fault = interrupt 8).

We remap them:
- **Master PIC**: IRQ 0-7 → Interrupts 32-39
- **Slave PIC**: IRQ 8-15 → Interrupts 40-47

## ISR Flow (Assembly → C)

```nasm
; interrupts.asm
isr_common_stub:
    pusha               ; Save all registers
    mov ax, 0x10        ; Load kernel data segment
    mov ds, ax
    call isr_handler    ; Call C function
    popa                ; Restore registers
    add esp, 8          ; Clean up error code + ISR number
    iret                ; Return from interrupt
```

## Key Files
- `kernel/idt.c/h` — IDT structures and `lidt`
- `kernel/isr.c/h` — C dispatcher and PIC remapping
- `kernel/interrupts.asm` — Assembly wrappers (32 ISRs + 16 IRQs)
