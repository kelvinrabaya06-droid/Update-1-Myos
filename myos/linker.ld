ENTRY(_start)
SECTIONS
{
    . = 1M;
    .multiboot : { *(.multiboot) }
    .text   : { *(.text) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data) }
    .bss    : { *(COMMON) *(.bss) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }
}
