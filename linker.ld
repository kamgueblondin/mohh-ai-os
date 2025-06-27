ENTRY(_start)

SECTIONS
{
    . = 1M;
    kernel_phys_start = .;

    .text :
    {
        *(.multiboot)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    .data :
    {
        *(.data .data.*)
    }

    .bss :
    {
        *(.bss .bss.*)
    }

    kernel_phys_end = ALIGN(4K);
}
