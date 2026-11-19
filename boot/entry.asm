format ELF
use32
section '.text' executable
public _start
public timer_isr
public fault_isr
extrn kmain
extrn ticks
extrn __bss_start
extrn __bss_end
_start:
    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    call kmain
.stop:
    cli
    hlt
    jmp .stop

timer_isr:
    push eax
    inc dword [ticks]
    mov al, 0x20
    out 0x20, al
    pop eax
    iretd

fault_isr:
    cli
    mov al, '!'
    out 0xe9, al
.stop:
    hlt
    jmp .stop

section '.note.GNU-stack'
