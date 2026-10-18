; KaOS user program startup. The kernel (kernel/elf.c) starts every
; program here with the stack holding [esp] = argc, [esp+4] = argv.
; Calls main(argc, argv) and passes its return value to SYS_EXIT, so
; programs may simply `return` from main.
[bits 32]
global _start
extern main

section .text
_start:
    mov eax, [esp]      ; argc
    mov ebx, [esp + 4]  ; argv
    push ebx
    push eax
    call main
    mov ebx, eax        ; exit code
    mov eax, 1          ; SYS_EXIT
    int 0x80
.hang:
    jmp .hang           ; Not reached

; Mark the stack non-executable (silences ld's GNU-stack warning)
section .note.GNU-stack noalloc noexec nowrite progbits
