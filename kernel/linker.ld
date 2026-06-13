ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    . = 0x1000; /* We start at address 0x1000 */

    .text :
    {
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        bss_start = .;
        *(.bss)
        bss_end = .;
    }
}
