[bits 16]
[org 0]

vbr_start:
    ; Set up segments (CS=0x9000, DS=0x9000)
    cli
    mov ax, cs
    mov ds, ax
    
    ; Set up a safe stack far away from the 0x1000 - 0x11000 loading zone
    mov ax, 0x9000
    mov ss, ax
    mov sp, 0xFFF0
    sti
    
    ; Save boot drive and selection
    mov [boot_select], bl
    mov [boot_drive], dl

    ; C kernel is loaded starting at physical 0x1000. 
    ; Let's use Segment 0x0100 : Offset 0x0000 
    mov ax, 0x0100
    mov es, ax
    xor bx, bx
    
    mov cx, 0x0003            ; CH=Track 0, CL=Sector 3
    mov dh, 0                 ; DH=Head 0
    mov dword [sectors_loaded], 0

    ; Reset disk system
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13

load_loop:
    mov byte [retry_count], 3 ; Floppy reads are flaky; retry with reset

.try_read:
    xor bx, bx                ; Force BX=0
    mov ah, 0x02
    mov al, 1
    mov dl, [boot_drive]

    push cx
    push dx
    push es
    int 0x13
    pop es
    pop dx
    pop cx
    jnc .read_ok

    ; Read failed: reset disk controller and retry
    dec byte [retry_count]
    jz disk_error
    push cx
    push dx
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13                  ; Reset disk system
    pop dx
    pop cx
    jmp .try_read

.read_ok:

    ; Print dot sequence to show loading progress
    push ax
    push bx
    mov ah, 0x0e
    mov al, '.'
    int 0x10
    pop bx
    pop ax

    ; Move ES forward by 512 bytes (0x20 paragraphs)
    mov ax, es
    add ax, 0x20
    mov es, ax

    ; Next sector geometry
    inc cl
    cmp cl, 19
    jne .continue
    
    mov cl, 1
    inc dh
    cmp dh, 2
    jne .continue
    
    mov dh, 0
    inc ch

.continue:
    inc dword [sectors_loaded]
    cmp dword [sectors_loaded], KERNEL_SECTORS   ; From build.sh (-D)
    jl load_loop

    ; Write selection to 0x0000:0x0914 (using ES=0)
    xor ax, ax
    mov es, ax
    mov bl, [boot_select]
    mov [es:0x0914], bl

    cmp byte [boot_select], 2
    je skip_vbe

    ; --- VESA/VBE SETUP (Bochs VBE extension — VirtualBox & QEMU) ---
    ; Step 1: Query VBE mode 0x115 to get the framebuffer physical address.
    ;         The LFB base address is the same for all modes (it's the PCI BAR).
    mov ax, 0x4F01
    mov cx, 0x115
    mov di, 0x0800          ; ModeInfoBlock buffer at ES:0x0800
    int 0x10
    cmp ax, 0x004F
    jne skip_vbe            ; VBE not supported at all

    mov eax, [es:0x0800 + 40]  ; PhysBasePtr — framebuffer physical address
    test eax, eax
    jz skip_vbe             ; Address is 0, BIOS is broken
    mov [es:0x0900], eax    ; Store fb_phys for the C kernel

    ; Step 2: Use Bochs VBE I/O registers to request 800x600 @ 32bpp.
    ;         Ports: 0x01CE = index, 0x01CF = data.
    ;         VBE_DISPI constants: 0=ID, 1=XRES, 2=YRES, 3=BPP, 4=ENABLE

    ; Disable display first
    mov dx, 0x01CE
    mov ax, 4               ; VBE_DISPI_INDEX_ENABLE
    out dx, ax
    mov dx, 0x01CF
    xor ax, ax
    out dx, ax

    ; XRES = 800
    mov dx, 0x01CE
    mov ax, 1
    out dx, ax
    mov dx, 0x01CF
    mov ax, 800
    out dx, ax

    ; YRES = 600
    mov dx, 0x01CE
    mov ax, 2
    out dx, ax
    mov dx, 0x01CF
    mov ax, 600
    out dx, ax

    ; BPP = 32
    mov dx, 0x01CE
    mov ax, 3
    out dx, ax
    mov dx, 0x01CF
    mov ax, 32
    out dx, ax

    ; Enable with Linear Framebuffer (bit 0 = ENABLE, bit 6 = LFB)
    mov dx, 0x01CE
    mov ax, 4
    out dx, ax
    mov dx, 0x01CF
    mov ax, 0x41
    out dx, ax

    ; Step 3: Store fixed VBE info for the C kernel (800x600x32, pitch=3200)
    mov dword [es:0x0904], 800      ; Width
    mov dword [es:0x0908], 600      ; Height
    mov dword [es:0x090C], 32       ; BPP
    mov dword [es:0x0910], 3200     ; Pitch = 800 * 4 bytes
    mov dword [es:0x0914], 1        ; is_graphical = true

    jmp pmode_switch

skip_vbe:
    mov dword [es:0x0914], 0      ; is_graphical = false

pmode_switch:
    cli
    ; Calculate linear address of GDT (cs * 16 + offset)
    mov eax, cs
    shl eax, 4
    add [gdt_phys_addr], eax
    
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp 0x08:0x1000

    jmp $

disk_error:
    mov ah, 0x0e
    mov al, 'E'
    int 0x10
    jmp $

vesa_error:
    mov ah, 0x0e
    mov al, 'V'
    int 0x10
    jmp $

; LOCAL VARIABLES (CS relative)
boot_drive db 0
boot_select db 0
retry_count db 0
sectors_loaded dd 0

gdt_start:
    dq 0
gdt_code:
    dw 0xffff, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xffff, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
gdt_phys_addr:
    dd gdt_start

times 512 - ($ - $$) db 0
