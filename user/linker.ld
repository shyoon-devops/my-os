ENTRY(_start)

PHDRS
{
    user PT_LOAD FLAGS(7);
}

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
    } :user

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
        *(.rela*)
        *(.rel*)
    }
}
