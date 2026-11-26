ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x00400000;
    .text : ALIGN(16)
    {
        KEEP(*(.text.start))
        *(.text .text.*)
    } :text

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    } :text

    /*
     * User memory permissions are page-granular. Keep mutable data and BSS
     * on their own page-aligned RW/NX load segment, separate from RX code.
     */
    . = ALIGN(0x1000);
    .data : ALIGN(16)
    {
        *(.data .data.*)
        *(.sdata .sdata.*)
    } :data

    .bss : ALIGN(16)
    {
        *(COMMON)
        *(.bss .bss.*)
        *(.sbss .sbss.*)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
        *(.gnu*)
    }
}
