; task_switch.asm - Level context switch with EFLAGS preservation
; void switch_context(uint32_t* old_esp, uint32_t new_esp)

[bits 32]
global switch_context

switch_context:
    ; Save flags and registers
    pushfd
    push ebp
    push edi
    push esi
    push ebx

    ; arg1 (&old_esp) is at [esp + 24]
    ; arg2 (new_esp)  is at [esp + 28]
    mov eax, [esp + 24]
    mov edx, [esp + 28]

    ; Save stack pointer
    mov [eax], esp

    ; Switch to new stack
    mov esp, edx

    ; Restore registers and flags
    pop ebx
    pop esi
    pop edi
    pop ebp
    popfd

    ret
