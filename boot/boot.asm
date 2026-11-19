format binary
use16
org 0x7c00

    jmp 0:start
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    cld
    sti
    mov [drive], dl
    mov bx, 0x8000
    mov si, 1
.next:
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
    cmp si, 9
    jb .next
    mov dl, [drive]
    jmp 0:0x8000

disk_error:
    mov si, error_message
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

drive db 0
error_message db 'Cobalt: disk read failed', 0
rb 510-($-$$)
dw 0xaa55
