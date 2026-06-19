// KolibriARM app: monitor
//
// Reads the process list, free-page count, and timer ticks once per loop
// iteration. Redraws roughly every second of wall time. Pressing 'q' exits.

.section .user.image.header, "a"
.global monitor_image_header
.type monitor_image_header, %function
monitor_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad monitor_image_end - monitor_image_header
    .quad monitor_start - monitor_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global monitor_start
.type monitor_start, %function

monitor_start:
    sub sp, sp, #160
    mov x19, sp

90:
    mov x0, #1
    adr x1, monitor_banner
    mov x2, monitor_banner_len
    bl common_write

    mov x0, x19
    mov x8, #101                  // sys_meminfo
    svc #0
    tbnz x0, #63, 95f
    mov x0, #1
    adr x1, monitor_free
    mov x2, monitor_free_len
    bl common_write
    ldr x0, [x19, #8]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, monitor_newline
    mov x2, #1
    bl common_write

    mov x0, x19
    mov x8, #100                  // sys_timeinfo
    svc #0
    tbnz x0, #63, 95f
    mov x0, #1
    adr x1, monitor_ticks
    mov x2, monitor_ticks_len
    bl common_write
    ldr x0, [x19]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, monitor_newline
    mov x2, #1
    bl common_write

    mov x0, x19
    mov x1, #4
    mov x8, #102                  // sys_proclist
    svc #0
    tbnz x0, #63, 95f
    mov x23, x0
    mov x24, x19
    mov x25, #0
    mov x0, #1
    adr x1, monitor_ps_header
    mov x2, monitor_ps_header_len
    bl common_write

91:
    cmp x25, x23
    b.hs 92f
    ldr w0, [x24]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, monitor_space
    mov x2, #1
    bl common_write
    ldr w0, [x24, #4]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, monitor_space
    mov x2, #1
    bl common_write
    add x0, x24, #8
    mov x1, #1
    bl common_write_cstr
    mov x0, #1
    adr x1, monitor_newline
    mov x2, #1
    bl common_write
    add x24, x24, #24
    add x25, x25, #1
    b 91b

92:
    mov x0, #0
    add x1, sp, #144
    mov x2, #1
    mov x8, #42                   // sys_read stdin
    svc #0
    cmp x0, #1
    b.ne 93f
    ldrb w0, [sp, #144]
    cmp w0, #'q'
    b.eq 96f

93:
    mov x8, #2
    svc #0
    b 90b                        // loop until the user presses 'q'

95:
    mov x0, #1
    adr x1, monitor_error
    mov x2, monitor_error_len
    bl common_write

96:
    add sp, sp, #160
    mov x0, #0
    mov x8, #1
    svc #0

.section .user.image.rodata, "a"
monitor_banner:
    .ascii "\nprocess monitor\n"
monitor_banner_len = . - monitor_banner
monitor_free:
    .ascii "free pages="
monitor_free_len = . - monitor_free
monitor_ticks:
    .ascii "timer ticks="
monitor_ticks_len = . - monitor_ticks
monitor_ps_header:
    .ascii "pid state name\n"
monitor_ps_header_len = . - monitor_ps_header
monitor_space:
    .ascii " "
monitor_newline:
    .ascii "\n"
monitor_error:
    .ascii "monitor: error\n"
monitor_error_len = . - monitor_error
monitor_image_end:
