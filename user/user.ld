/* KaOS user programs: linked at the start of the per-process user region
   (virtual 0x40000000; every process has its own pages there).
   Must match USER_BASE in kernel/paging.h. */
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }
}
