OUTPUT_ARCH("riscv")
ENTRY(main)
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x1000;
    .text : { *(.text.entry) *(.text.main) *(.text .text.*) } :text
    .rodata : { . = ALIGN(16); *(.srodata .srodata.*) *(.rodata .rodata.*) } :text
    . = ALIGN(4096);
    .data : { *(.sdata .sdata.*) *(.data .data.*) } :data
    .bss : { . = ALIGN(16); *(.sbss .sbss.*) *(.bss .bss.*) } :data
}
