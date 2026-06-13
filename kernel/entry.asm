[bits 32]
[extern kernel_main] ; Defined in main.c
[extern bss_start]
[extern bss_end]
global _start

_start:
    ; Set up 32-bit data segments
    mov ax, 0x10 ; Data segment selector from GDT
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Set up stack
    mov ebp, 0x90000
    mov esp, ebp

    ; Zero out the BSS section
    cld              ; rep stosb must go forward; GCC code also assumes DF=0
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    xor al, al
    rep stosb

    call kernel_main ; Jump to our C function
    jmp $            ; Should never return
