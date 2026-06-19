// KolibriARM app: clock
//
// Creates a window that displays the current uptime as HH:MM:SS. Reads
// timer ticks via SYS_TIMEINFO (syscall 100). The kernel's generic timer
// fires at 100 Hz, so dividing ticks by 100 yields seconds since boot.
// Redraws once per second using a yield-based delay. 'q' closes the app.

#define SYS_EXIT         1
#define SYS_YIELD        2
#define SYS_TIMEINFO   100
#define SYS_WINDOW_CREATE    70
#define SYS_WINDOW_DRAW_TEXT 72
#define SYS_WINDOW_DRAW_RECT 73
#define SYS_WINDOW_REDRAW    76
#define SYS_WINDOW_EVENT     74
#define SYS_WINDOW_DESTROY  71
#define SYS_WINDOW_SET_TITLE 75

.equ WIN_W, 200
.equ WIN_H, 80
.equ EVENT_CAP, 4
.equ YIELDS_PER_SEC, 200
.equ TITLE_BAR_H, 12

.section .user.image.header, "a"
.global clock_image_header
.type clock_image_header, %function
clock_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad clock_image_end - clock_image_header
    .quad clock_start - clock_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global clock_start
.type clock_start, %function

clock_start:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #128

    // timeinfo buffer at sp[0..23] (3 uint64_t).
    // event buffer at sp[24..71] (EVENT_CAP * 12 bytes).
    // text buffer at sp[80..95] (16 bytes for "HH:MM:SS\0").

    add x20, sp, #80                 // x20 = text buffer

    mov x0, #1
    adr x1, clock_banner
    bl common_write_cstr

    // Create window at (440, 64), near the upper-right of the desktop.
    mov x0, #440
    mov x1, #64
    mov x2, #WIN_W
    mov x3, #WIN_H
    movz x4, #0xff20, lsl #16
    movk x4, #0x2830                 // bg: dark gray-blue
    movz x5, #0xff80, lsl #16
    movk x5, #0x8080                 // border: gray
    adr x6, clock_title
    mov x8, #SYS_WINDOW_CREATE
    svc #0
    cmp x0, #0
    b.lt clock_fail
    mov x19, x0                      // x19 = window handle

    // Ask the kernel to paint a 12 px title bar with the "clock" title.
    // x0 = window id, x1 = title ptr, x2 = title_h.
    mov x0, x19
    adr x1, clock_title
    mov x2, #TITLE_BAR_H
    mov x8, #SYS_WINDOW_SET_TITLE
    svc #0

    bl clock_redraw

clock_loop:
    // Yield K times to wait ~1 second. Each yield lets other processes
    // run; on a single-core system we get roughly 100 ticks/sec.
    mov x21, #YIELDS_PER_SEC
1:
    mov x0, #0
    mov x8, #SYS_YIELD
    svc #0
    sub x21, x21, #1
    cbnz x21, 1b

    bl clock_redraw

    // Drain any pending events (we don't react to them, just keep the
    // queue empty so it does not fill up).
    add x1, sp, #24
    mov x0, x19
    mov x2, #EVENT_CAP
    mov x8, #SYS_WINDOW_EVENT
    svc #0
    cmp x0, #0
    b.le clock_loop

    // We got >= 1 event. If any of them was a close, exit.
    mov x22, x0                      // n events
    mov x23, #0
2:
    cmp x23, x22
    b.ge clock_loop
    mov x24, x23
    mov x25, #12
    mul x24, x24, x25
    add x24, x24, #24               // event buffer starts at sp + 24
    mov x26, sp
    add x24, x26, x24
    ldr w25, [x24]                  // event type
    cmp w25, #6                     // GUI_EVENT_CLOSE
    b.eq clock_exit
    add x23, x23, #1
    b 2b

clock_exit:
    mov x0, x19
    mov x8, #SYS_WINDOW_DESTROY
    svc #0
    mov x0, #0
    mov x8, #SYS_EXIT
    svc #0

clock_fail:
    mov x0, #1
    adr x1, clock_fail_msg
    bl common_write_cstr
    mov x0, #1
    mov x8, #SYS_EXIT
    svc #0

// clock_redraw: rebuild the HH:MM:SS string from timer_ticks and draw it.
clock_redraw:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    // Fetch timeinfo.
    mov x0, sp
    mov x8, #SYS_TIMEINFO
    svc #0
    tbnz x0, #63, 9f                // if negative, skip redraw

    // x0..x2 ignored after SVC; reload ticks from stack.
    ldr x0, [sp]                    // x0 = timer_ticks (at 100 Hz)
    mov x1, #100
    udiv x2, x0, x1                 // x2 = total seconds
    // Compute HH = total / 3600.
    mov x3, #3600
    udiv x4, x2, x3                 // x4 = HH
    msub x5, x4, x3, x2             // x5 = remainder seconds
    mov x3, #60
    udiv x6, x5, x3                 // x6 = MM
    msub x7, x6, x3, x5             // x7 = SS

    // Format HH:MM:SS into the caller's text buffer (x20).
    // Two-digit zero-padded numbers using ASCII '0'..'9'.
    mov x9, x4
    mov x10, #10
    udiv x11, x9, x10
    msub x12, x11, x10, x9
    add x11, x11, #'0'
    add x12, x12, #'0'
    strb w11, [x20, #0]
    strb w12, [x20, #1]
    mov w13, #':'
    strb w13, [x20, #2]

    mov x9, x6
    udiv x11, x9, x10
    msub x12, x11, x10, x9
    add x11, x11, #'0'
    add x12, x12, #'0'
    strb w11, [x20, #3]
    strb w12, [x20, #4]
    strb w13, [x20, #5]

    mov x9, x7
    udiv x11, x9, x10
    msub x12, x11, x10, x9
    add x11, x11, #'0'
    add x12, x12, #'0'
    strb w11, [x20, #6]
    strb w12, [x20, #7]
    mov w14, #0
    strb w14, [x20, #8]

    // Clear previous text area, draw new string centered.
    mov x0, x19
    mov x1, #20
    mov x2, #30
    mov x3, #(WIN_W - 40)
    mov x4, #28
    movz x5, #0xff20, lsl #16
    movk x5, #0x2830
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    mov x0, x19
    mov x1, #40
    mov x2, #34
    movz x3, #0xffe0, lsl #16
    movk x3, #0xe8f0
    mov x4, x20
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0

    mov x0, x19
    mov x8, #SYS_WINDOW_REDRAW
    svc #0
9:
    ldp x29, x30, [sp], #16
    ret

.section .user.image.rodata, "a"
clock_title:
    .ascii "clock\0"
clock_banner:
    .ascii "clock: starting\n\0"
clock_fail_msg:
    .ascii "clock: window create failed\n\0"
clock_image_end:
