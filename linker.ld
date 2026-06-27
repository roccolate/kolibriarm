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

    /*
     * The kernel has no unwinder or hosted runtime. Keep compiler/linker
     * metadata out of the loadable binary so the size limit tracks executable
     * code, embedded apps, and real read-only data only.
     */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }
}
