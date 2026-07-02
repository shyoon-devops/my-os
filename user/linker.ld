ENTRY(_start)

SECTIONS
{
    . = 0x50000000;

    .text : ALIGN(0x1000)
    {
        *(.text*)
        *(.rodata*)
        *(.data*)
        *(COMMON)
        *(.bss*)
    }
}
