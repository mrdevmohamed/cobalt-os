OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x10000;
    .text : { *(.text) *(.text.*) } :code
    .rodata : { *(.rodata) *(.rodata.*) } :code
    .data : { *(.data) *(.data.*) } :data
    .bss : {
        __bss_start = .;
        *(COMMON) *(.bss) *(.bss.*)
        __bss_end = .;
    } :data
    ASSERT(__bss_end < 0x80000, "Kernel overlaps boot stack")
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
