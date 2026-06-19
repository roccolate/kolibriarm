// KolibriARM app: monitor
//
// Windowed process monitor. It owns a desktop window, redraws memory/tick
// state plus a short process list, and exits on 'q' or title-bar close.

#define SYS_EXIT         1
#define SYS_YIELD        2
#define SYS_MEMINFO    101
#define SYS_TIMEINFO   100
#define SYS_PROCLIST   102
#define SYS_WINDOW_CREATE    70
#define SYS_WINDOW_DESTROY   71
#define SYS_WINDOW_DRAW_TEXT 72
#define SYS_WINDOW_DRAW_RECT 73
#define SYS_WINDOW_EVENT     74
#define SYS_WINDOW_SET_TITLE 75
#define SYS_WINDOW_REDRAW    76

.equ WIN_X,          56
.equ WIN_Y,          120
.equ WIN_W,          340
.equ WIN_H,          220
.equ TITLE_BAR_H,    12
.equ EVENT_CAP,      8
.equ PROC_CAP,       6
.equ REDRAW_WAIT,    20
.equ OFF_MEM,        0
.equ OFF_EVENTS,     32
.equ OFF_PROCS,      128
.equ OFF_NUM,        288
.equ FRAME_BYTES,    352

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
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #FRAME_BYTES

    add x20, sp, #OFF_MEM           // time/memory info scratch
    add x21, sp, #OFF_EVENTS        // window event buffer
    add x22, sp, #OFF_PROCS         // process-list buffer
    add x23, sp, #OFF_NUM           // decimal text buffer

    mov x0, #1
    adr x1, monitor_banner
    bl common_write_cstr

    bl monitor_create_window
    bl monitor_redraw
    mov x28, #REDRAW_WAIT

monitor_loop:
    mov x0, x19
    mov x1, x21
    mov x2, #EVENT_CAP
    mov x8, #SYS_WINDOW_EVENT
    svc #0
    cmp x0, #0
    b.gt monitor_handle_events

    sub x28, x28, #1
    cbnz x28, monitor_loop_yield
    bl monitor_redraw
    mov x28, #REDRAW_WAIT

monitor_loop_yield:
    mov x0, #0
    mov x8, #SYS_YIELD
    svc #0
    b monitor_loop

monitor_handle_events:
    mov x24, x0                      // event count
    mov x25, #0
1:
    cmp x25, x24
    b.ge monitor_loop
    mov x26, #12
    mul x26, x25, x26
    add x27, x21, x26
    ldr w26, [x27]                   // type
    ldr w27, [x27, #4]               // data1
    cmp w26, #6                      // GUI_EVENT_CLOSE
    b.eq monitor_exit
    cmp w26, #1                      // GUI_EVENT_KEY_PRESS
    b.ne 2f
    cmp w27, #'q'
    b.eq monitor_exit
    cmp w27, #'Q'
    b.eq monitor_exit
2:
    add x25, x25, #1
    b 1b

monitor_exit:
    mov x0, x19
    mov x8, #SYS_WINDOW_DESTROY
    svc #0
    add sp, sp, #FRAME_BYTES
    ldp x29, x30, [sp], #16
    mov x0, #0
    mov x8, #SYS_EXIT
    svc #0

monitor_fail:
    mov x0, #1
    adr x1, monitor_fail_msg
    bl common_write_cstr
    add sp, sp, #FRAME_BYTES
    ldp x29, x30, [sp], #16
    mov x0, #1
    mov x8, #SYS_EXIT
    svc #0

monitor_create_window:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    mov x0, #WIN_X
    mov x1, #WIN_Y
    mov x2, #WIN_W
    mov x3, #WIN_H
    movz x4, #0xff18, lsl #16
    movk x4, #0x2024
    movz x5, #0xff80, lsl #16
    movk x5, #0x9080
    adr x6, monitor_title
    mov x8, #SYS_WINDOW_CREATE
    svc #0
    cmp x0, #0
    b.lt 1f
    mov x19, x0

    mov x0, x19
    adr x1, monitor_title
    mov x2, #TITLE_BAR_H
    mov x8, #SYS_WINDOW_SET_TITLE
    svc #0

    ldp x29, x30, [sp], #16
    ret
1:
    ldp x29, x30, [sp], #16
    b monitor_fail

monitor_redraw:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    mov x0, x19
    mov x1, #1
    mov x2, #0
    mov x3, #(WIN_W - 2)
    mov x4, #(WIN_H - TITLE_BAR_H - 2)
    movz x5, #0xff18, lsl #16
    movk x5, #0x2024
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    mov x1, #12
    mov x2, #8
    adr x4, monitor_heading
    bl monitor_draw_text

    mov x0, x20
    mov x8, #SYS_MEMINFO
    svc #0
    tbnz x0, #63, 1f
    mov x1, #12
    mov x2, #28
    adr x4, monitor_free
    bl monitor_draw_text
    ldr x0, [x20, #8]
    mov x1, x23
    bl monitor_format_dec
    mov x1, #96
    mov x2, #28
    mov x4, x23
    bl monitor_draw_text

1:
    mov x0, x20
    mov x8, #SYS_TIMEINFO
    svc #0
    tbnz x0, #63, 2f
    mov x1, #12
    mov x2, #44
    adr x4, monitor_ticks
    bl monitor_draw_text
    ldr x0, [x20]
    mov x1, x23
    bl monitor_format_dec
    mov x1, #96
    mov x2, #44
    mov x4, x23
    bl monitor_draw_text

2:
    mov x1, #12
    mov x2, #68
    adr x4, monitor_ps_header
    bl monitor_draw_text

    mov x0, x22
    mov x1, #PROC_CAP
    mov x8, #SYS_PROCLIST
    svc #0
    tbnz x0, #63, 8f
    mov x24, x0                      // process count
    mov x25, #0
    mov x26, x22                     // current entry
    mov x27, #84                     // current y
3:
    cmp x25, x24
    b.ge 8f

    ldr w0, [x26]                    // pid
    mov x1, x23
    bl monitor_format_dec
    mov x1, #12
    mov x2, x27
    mov x4, x23
    bl monitor_draw_text

    ldr w0, [x26, #4]                // state
    mov x1, x23
    bl monitor_format_dec
    mov x1, #56
    mov x2, x27
    mov x4, x23
    bl monitor_draw_text

    mov x1, #108
    mov x2, x27
    add x4, x26, #8
    bl monitor_draw_text

    add x26, x26, #24
    add x25, x25, #1
    add x27, x27, #16
    b 3b

8:
    mov x0, x19
    mov x8, #SYS_WINDOW_REDRAW
    svc #0
    ldp x29, x30, [sp], #16
    ret

// monitor_draw_text: x1=x, x2=y, x4=cstr. Uses the standard monitor color.
monitor_draw_text:
    mov x0, x19
    movz x3, #0xffd0, lsl #16
    movk x3, #0xe0d0
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0
    ret

// monitor_format_dec: x0=value, x1=buffer. Writes a null-terminated decimal.
monitor_format_dec:
    mov x2, x1                       // buffer
    cbnz x0, 1f
    mov w3, #'0'
    strb w3, [x2]
    mov w3, #0
    strb w3, [x2, #1]
    ret

1:
    mov x3, #0                       // digit count
    mov x4, x0
    mov x5, #10
2:
    udiv x6, x4, x5
    msub x7, x6, x5, x4
    add w7, w7, #'0'
    strb w7, [x2, x3]
    add x3, x3, #1
    mov x4, x6
    cbnz x4, 2b

    mov x4, #0
3:
    cmp x4, x3
    b.hs 5f
    sub x5, x3, x4
    sub x5, x5, #1
    cmp x4, x5
    b.hs 5f
    ldrb w6, [x2, x4]
    ldrb w7, [x2, x5]
    strb w7, [x2, x4]
    strb w6, [x2, x5]
    add x4, x4, #1
    b 3b
5:
    mov w4, #0
    strb w4, [x2, x3]
    ret

.section .user.image.rodata, "a"
monitor_title:
    .ascii "monitor\0"
monitor_banner:
    .ascii "monitor: starting\n\0"
monitor_fail_msg:
    .ascii "monitor: window create failed\n\0"
monitor_heading:
    .ascii "SYSTEM MONITOR\0"
monitor_free:
    .ascii "FREE PG\0"
monitor_ticks:
    .ascii "TICKS\0"
monitor_ps_header:
    .ascii "PID   ST    NAME\0"
monitor_image_end:
