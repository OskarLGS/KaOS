; ----------------------------------------------------------------------------------------
; KaOS Recovery Mode
; ----------------------------------------------------------------------------------------
[bits 16]
[org 0]              ; KaBooM chainloads us at 0x9000:0000 with a far jump

    mov ax, cs       ; DS must match CS so lodsb finds our strings
    mov ds, ax
    mov si, msg
    call print_string
    jmp $

print_string:
    mov ah, 0x0e
.loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .loop
.done:
    ret

msg db '--- KaOS Recovery Mode ---', 10, 13, 'System diagnostics not yet implemented.', 0

times 510 - ($ - $$) db 0
dw 0xaa55
