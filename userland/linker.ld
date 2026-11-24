ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
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

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
        *(.gnu*)
    }
}
