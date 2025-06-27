ENTRY(_start)

PHDRS
{
    kernel_text PT_LOAD FILEHDR PHDRS FLAGS(5) ; RX : Read(4) + Execute(1)
    kernel_rodata PT_LOAD FLAGS(4)             ; R  : Read(4)
    kernel_data PT_LOAD FLAGS(6)               ; RW : Read(4) + Write(2)
}

SECTIONS
{
    . = 1M;
    kernel_phys_start = .;

    .multiboot ALIGN(4K) :
    {
        *(.multiboot)
    } :kernel_text

    .text ALIGN(4K) :
    {
        *(.text .text.*)
    } :kernel_text

    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    } :kernel_rodata

    .data ALIGN(4K) :
    {
        *(.data .data.*)
    } :kernel_data

    .bss ALIGN(4K) :
    {
        *(.bss .bss.*)
    } :kernel_data

    kernel_phys_end = ALIGN(4K);
}
