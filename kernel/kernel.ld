/* kernel/kernel.ld — flat binary loaded at 0x8400 by the boot stage */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS
{
    . = 0x8400;
    .text : { *(.entry) *(.text*) }
    .rodata : ALIGN(4) { *(.rodata*) }
    .data : ALIGN(4) { *(.data*) }
    __bss_start = .;
    .bss : ALIGN(4) { *(.bss*) *(COMMON) }
    __bss_end = .;
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
