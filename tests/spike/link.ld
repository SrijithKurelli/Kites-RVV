OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x80000000;
    .text : ALIGN(16)
    {
        KEEP(*(.text.start))
        *(.text .text.*)
    }
    .rodata : ALIGN(16) { *(.rodata .rodata.*) }
    .data : ALIGN(16) { *(.data .data.*) *(.sdata .sdata.*) }
    .bss : ALIGN(16) { *(.bss .bss.*) *(COMMON) *(.sbss .sbss.*) }
}
