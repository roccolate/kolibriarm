// KolibriARM app: win
//
// Creates a window via SYS_WINDOW_CREATE, draws a colored rectangle and a
// title, then loops reading events. Used to validate the 10.2 window
// syscall path end-to-end.

#define SYS_EXIT         1
#define SYS_YIELD        2
#define SYS_GETPID       3
#define SYS_WRITE       43
#define SYS_WINDOW_CREATE    70
#define SYS_WINDOW_DRAW_RECT 73
#define SYS_WINDOW_DRAW_TEXT 72
#define SYS_WINDOW_REDRAW    76
#define SYS_WINDOW_EVENT     74

.equ WIN_W, 320
.equ WIN_H, 200
.equ EVENT_CAP, 8

.section .user.image.header, "a"
.global win_image_header
.type win_image_header, %function
win_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad win_image_end - win_image_header
    .quad win_start - win_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global win_start
.type win_start, %function

win_start:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    // 8 events * 12 bytes = 96; round up to 128.
    sub sp, sp, #128

    mov x0, #1
    adr x1, win_uart_pre
    bl common_write_cstr

    // Create the window.
    mov x0, #80                      // x
    mov x1, #64                      // y
    mov x2, #WIN_W
    mov x3, #WIN_H
    movz x4, #0xff20, lsl #16
    movk x4, #0x2830
    movz x5, #0xff80, lsl #16
    movk x5, #0x8080
    adr x6, win_title
    mov x8, #SYS_WINDOW_CREATE
    svc #0
    cmp x0, #0
    b.lt win_fail
    mov x19, x0                      // x19 = window_id (callee-saved)

    mov x0, #1
    adr x1, win_created
    bl common_write_cstr

    // Red rect.
    mov x0, x19
    mov x1, #20
    mov x2, #40
    mov x3, #120
    mov x4, #40
    movz x5, #0xffa0, lsl #16
    movk x5, #0x4040
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    // Green rect.
    mov x0, x19
    mov x1, #160
    mov x2, #40
    mov x3, #120
    mov x4, #40
    movz x5, #0xff40, lsl #16
    movk x5, #0xa060
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    // Title text.
    mov x0, x19
    mov x1, #12
    mov x2, #100
    movz x3, #0xffff, lsl #16
    movk x3, #0xffff
    adr x4, win_text_msg
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0

    // Trigger a redraw.
    mov x0, x19
    mov x8, #SYS_WINDOW_REDRAW
    svc #0

    mov x0, #1
    adr x1, win_ready
    bl common_write_cstr

    // Event loop.
    add x20, sp, #0                  // x20 = event buf base
    mov x21, #0                      // total events received

win_loop:
    mov x0, x19
    mov x1, x20
    mov x2, #EVENT_CAP
    mov x8, #SYS_WINDOW_EVENT
    svc #0
    mov x22, x0                      // n events returned
    cmp x22, #0
    b.le win_loop_yield

    add x21, x21, x22

    // Print "evs=" + x22 + "\n" so the host sees the path worked.
    mov x0, #1
    adr x1, win_evs_pre
    bl common_write_cstr
    mov x0, #1
    mov x1, x22
    bl common_print_dec
    mov x0, #1
    adr x1, win_nl
    bl common_write_cstr

    // Walk the events looking for KEY_PRESS with data1 == 'q' (0x71).
    mov x23, #0
1:  cmp x23, x22
    b.ge 2f

    // Each event is 12 bytes (3 x uint32). Base offset = x23 * 12.
    mov x24, x23
    mov x25, #12
    mul x24, x24, x25
    add x26, x20, x24                // x26 = &event[i]

    ldr w27, [x26]                   // type
    ldr w28, [x26, #4]               // data1

    cmp w27, #1                      // GUI_EVENT_KEY_PRESS
    b.ne 3f
    cmp w28, #0x71                   // 'q'
    b.ne 3f

    mov x0, #1
    adr x1, win_quit
    bl common_write_cstr
    mov x0, #21
    mov x8, #SYS_EXIT
    svc #0
3:
    add x23, x23, #1
    b 1b
2:

win_loop_yield:
    mov x0, #0
    mov x8, #SYS_YIELD
    svc #0
    b win_loop

win_fail:
    mov x0, #1
    adr x1, win_fail_msg
    bl common_write_cstr
    mov x0, #1
    mov x8, #SYS_EXIT
    svc #0

.section .user.image.rodata, "a"
win_title:
    .ascii "win\0"
win_text_msg:
    .ascii "Hello from window\0"
win_uart_pre:
    .ascii "win: start\n\0"
win_created:
    .ascii "win: created\n\0"
win_ready:
    .ascii "win: redraw ok\n\0"
win_evs_pre:
    .ascii "win: evs=\0"
win_nl:
    .ascii "\n\0"
win_quit:
    .ascii "win: quit\n\0"
win_fail_msg:
    .ascii "win: window create failed\n\0"
win_image_end:
