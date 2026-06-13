/* KaOS user programs: linked at the start of the Ring 3 region (10 MB).
   Must match USER_BASE in kernel/paging.h. */
ENTRY(_start)

SECTIONS
{
    . = 0xA00000;

    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }
}
