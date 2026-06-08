ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0x40080000;
    __kernel_start = .;

    .text : ALIGN(8) {
        KEEP(*(.text.boot))
        *(.text*)
    } :text

    .rodata : ALIGN(8) {
        *(.rodata*)
    } :rodata

    .data : ALIGN(8) {
        *(.data*)
    } :data

    .bss : ALIGN(16) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } :data

    . = ALIGN(16);
    . += 0x4000;
    __stack_top = .;
    __kernel_end = .;
}
