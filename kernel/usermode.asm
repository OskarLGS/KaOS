[bits 32]

; void gdt_flush(uint32_t gdt_ptr_addr)
; Load the new GDT and reload every segment register.
global gdt_flush
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10        ; Kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush     ; Far jump reloads CS
.flush:
    ret

; void tss_flush(void)
global tss_flush
tss_flush:
    mov ax, 0x28        ; TSS selector
    ltr ax
    ret

; void enter_user_mode(uint32_t entry, uint32_t user_stack_top)
; Builds an iret frame for Ring 3 and drops privilege. Never returns.
global enter_user_mode
enter_user_mode:
    mov ebx, [esp + 4]  ; Program entry point
    mov ecx, [esp + 8]  ; User stack top

    mov ax, 0x23        ; User data selector (RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push 0x23           ; SS
    push ecx            ; ESP
    push 0x202          ; EFLAGS (IF=1: interrupts stay enabled in user mode)
    push 0x1B           ; CS (user code, RPL 3)
    push ebx            ; EIP
    iretd
