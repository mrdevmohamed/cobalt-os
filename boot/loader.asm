format binary
use16
org 0x8000
include 'kernel-size.inc'

    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x3ff0
    cld
    sti
    mov es, ax
    mov [drive], dl
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov si, 9
.load:
    mov ax, si
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    xor dx, dx
    mov di, 2
    div di
    mov ch, al
    mov dh, dl
    mov dl, [drive]
    mov ax, 0x0201
    push si
    push bx
    int 0x13
    pop bx
    pop si
    jc disk_error
    add bx, 512
    inc si
    cmp si, 9 + KERNEL_SECTORS
    jb .load

    ; Preserve the firmware's 8x16 glyphs before leaving real mode.
    mov ax, 0x1130
    mov bh, 6
    int 0x10
    mov si, bp
    mov ax, es
    mov ds, ax
    xor ax, ax
    mov es, ax
    mov di, 0x7000
    mov cx, 2048
    rep movsw
    mov ds, ax

    ; Enumerate VBE modes rather than relying on vendor mode numbers.
    mov dword [0x4000], 'VBE2'
    mov di, 0x4000
    mov ax, 0x4f00
    int 0x10
    cmp ax, 0x004f
    jne video_error
    mov si, [0x400e]
    mov ax, [0x4010]
    mov fs, ax
.mode:
    mov cx, [fs:si]
    add si, 2
    cmp cx, 0xffff
    je video_error
    mov [mode], cx
    push fs
    push si
    mov di, 0x5000
    mov ax, 0x4f01
    int 0x10
    pop si
    pop fs
    cmp ax, 0x004f
    jne .mode
    mov ax, [0x5000]
    and ax, 0x0091
    cmp ax, 0x0091
    jne .mode
    cmp word [0x5012], 800
    jne .mode
    cmp word [0x5014], 600
    jne .mode
    cmp byte [0x5019], 32
    jne .mode
    cmp byte [0x501b], 6
    jne .mode
    ; The renderer requires the conventional 8:8:8 BGR layout.
    cmp word [0x501f], 0x1008
    jne .mode
    cmp word [0x5021], 0x0808
    jne .mode
    cmp word [0x5023], 0x0008
    jne .mode
    mov eax, [0x5028]
    mov [0x6000], eax
    mov ax, [0x5010]
    mov [0x6004], ax
    mov bx, [mode]
    or bx, 0x4000
    mov ax, 0x4f02
    int 0x10
    cmp ax, 0x004f
    jne video_error

    cli
    in al, 0x92
    or al, 2
    and al, 0xfe
    out 0x92, al
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

video_error:
    mov si, video_message
    jmp fail
disk_error:
    mov si, disk_message
fail:
    xor ax, ax
    mov ds, ax
.print:
    lodsb
    test al, al
    jz .stop
    mov ah, 0x0e
    int 0x10
    jmp .print
.stop:
    cli
    hlt
    jmp .stop

align 8
gdt:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
gdt_descriptor:
    dw $-gdt-1
    dd gdt
drive db 0
mode dw 0
video_message db 'Cobalt: 800x600x32 VBE mode unavailable', 0
disk_message db 'Cobalt: kernel disk read failed', 0

use32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    cld
    jmp 0x10000

rb 4096-($-$$)
