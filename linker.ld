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

    .app_hello : ALIGN(16) {
        __app_hello_start = .;
        KEEP(*(.app_hello_blob*))
        __app_hello_end = .;
    } :text

    .app_loop : ALIGN(16) {
        __app_loop_start = .;
        KEEP(*(.app_loop_blob*))
        __app_loop_end = .;
    } :text

    .app_fault : ALIGN(16) {
        __app_fault_start = .;
        KEEP(*(.app_fault_blob*))
        __app_fault_end = .;
    } :text

    .app_shell : ALIGN(16) {
        __app_shell_start = .;
        KEEP(*(.app_shell_blob*))
        __app_shell_end = .;
    } :text

    .app_editor : ALIGN(16) {
        __app_editor_start = .;
        KEEP(*(.app_editor_blob*))
        __app_editor_end = .;
    } :text

    .app_monitor : ALIGN(16) {
        __app_monitor_start = .;
        KEEP(*(.app_monitor_blob*))
        __app_monitor_end = .;
    } :text

    .app_win : ALIGN(16) {
        __app_win_start = .;
        KEEP(*(.app_win_blob*))
        __app_win_end = .;
    } :text

    .app_panel : ALIGN(16) {
        __app_panel_start = .;
        KEEP(*(.app_panel_blob*))
        __app_panel_end = .;
    } :text

    .app_clock : ALIGN(16) {
        __app_clock_start = .;
        KEEP(*(.app_clock_blob*))
        __app_clock_end = .;
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
