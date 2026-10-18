[bits 16]
[org 0x0600]

start:
    ; 1. Fix Segments and Stack
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti

    ; 2. RELOCATION: Move from 0x7c00 to 0x0600
    cld                 ; Ensure forward copy (BIOS does not guarantee DF=0)
    mov si, 0x7c00
    mov di, 0x0600
    mov cx, 256
    rep movsw
    
    jmp 0:relocated

relocated:
    ; 3. Save Boot Drive (provided by BIOS in DL, preserved across rep movsw)
    mov [boot_drive], dl

    ; Clear screen
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    call render_menu

get_input:
    mov ah, 0x00
    int 0x16
    
    cmp ah, 0x48    ; Up
    je move_up
    cmp ah, 0x50    ; Down
    je move_down
    cmp al, 0x0d    ; Enter
    je do_boot
    jmp get_input

move_up:
    dec byte [selected]
    jns .ok
    mov byte [selected], 2
.ok:
    call render_menu
    jmp get_input

move_down:
    inc byte [selected]
    cmp byte [selected], 3
    jl .ok
    mov byte [selected], 0
.ok:
    call render_menu
    jmp get_input

render_menu:
    ; Reset cursor
    mov ah, 0x02
    xor bh, bh
    xor dx, dx
    int 0x10

    mov si, title_msg
    call print_string

    ; Option 1
    mov al, ' '
    cmp byte [selected], 0
    jne .p1
    mov al, '>'
.p1:
    call print_char
    mov si, os_a_msg
    call print_string

    ; Option 2
    mov al, ' '
    cmp byte [selected], 1
    jne .p2
    mov al, '>'
.p2:
    call print_char
    mov si, os_b_msg
    call print_string

    ; Option 3
    mov al, ' '
    cmp byte [selected], 2
    jne .p3
    mov al, '>'
.p3:
    call print_char
    mov si, os_c_msg
    call print_string
    ret

do_boot:
    ; Clear screen to confirm "Enter" was caught
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    mov bl, [selected]
    cmp bl, 1
    je .load_recovery
    mov si, part_1
    jmp chainload
.load_recovery:
    mov si, part_2

chainload:
    ; Convert partition LBA to CHS (1.44MB floppy: 18 sectors/track, 2 heads)
    mov ax, [si + 8]    ; Partition LBA (low word; floppy fits in 16 bits)
    xor dx, dx
    mov bx, 18
    div bx              ; AX = track index, DX = sector within track (0-17)
    inc dl
    mov cl, dl          ; CL = sector (1-18)
    xor dx, dx
    mov bx, 2
    div bx              ; AX = cylinder, DX = head
    mov ch, al          ; CH = cylinder
    mov dh, dl          ; DH = head
    mov dl, [boot_drive]

    mov ax, 0x9000
    mov es, ax
    xor bx, bx          ; Buffer at 0x9000:0000

    mov byte [retry_count], 3   ; Retry flaky reads with a controller reset
.try_read:
    mov ah, 0x02
    mov al, 1
    int 0x13
    jnc .read_ok
    dec byte [retry_count]
    jz disk_error
    push cx
    push dx
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13            ; Reset disk system
    pop dx
    pop cx
    jmp .try_read

.read_ok:
    mov bl, [selected]   ; Retrieve selection
    mov dl, [boot_drive] ; Retrieve drive
    jmp 0x9000:0x0000

disk_error:
    mov al, 'E'
    call print_char
    jmp halt

print_char:
    mov ah, 0x0e
    int 0x10
    ret

print_string:
    mov ah, 0x0e
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    ret

halt:
    cli
    hlt
    jmp halt

boot_drive  db 0
selected    db 0
retry_count db 0
title_msg   db '--- KaBooM v1.0.0 ---', 13, 10, 0
os_a_msg    db ' 1. KaOS', 13, 10, 0
os_b_msg    db ' 2. Recovery Mode', 13, 10, 0
os_c_msg    db ' 3. KaOS (Text Mode)', 13, 10, 0
error_msg   db 'Error!', 0

times 446 - ($ - $$) db 0

part_1:
    db 0x80, 0, 2, 0, 0x83, 0, 2, 0
    dd 1    ; Kernel bridge (VBR) at LBA 1 (2nd sector)
    dd 1
part_2:
    db 0x00, 0, 0, 0, 0x83, 0, 0, 0
    dd 2 + KERNEL_SECTORS  ; Recovery right after MBR + bridge + kernel (build.sh -D)
    dd 1

times 32 db 0 ; Empty partitions
dw 0xaa55
