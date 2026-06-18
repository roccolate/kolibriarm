// KolibriARM app: panel
//
// Draws a taskbar at the bottom of the screen with one launcher button
// per registered app. On click, sys_spawn the app. Used as the entry
// point for the Phase 10.3 "panel + click-to-launch" milestone.

#define SYS_EXIT         1
#define SYS_YIELD        2
#define SYS_GETPID       3
#define SYS_WRITE       43
#define SYS_SPAWN        4
#define SYS_WINDOW_CREATE    70
#define SYS_WINDOW_DRAW_RECT 73
#define SYS_WINDOW_DRAW_TEXT 72
#define SYS_WINDOW_REDRAW    76
#define SYS_WINDOW_EVENT     74
#define SYS_WINDOW_DESTROY  71

// Geometry constants for a 640x480 virtio-gpu framebuffer.
.equ SCREEN_W,      640
.equ SCREEN_H,      480
.equ PANEL_H,       32
.equ PANEL_Y,       (SCREEN_H - PANEL_H)        // 448
.equ BTN_H,         24
.equ BTN_Y,         (PANEL_Y + 4)               // 452
.equ BTN_W,         80
.equ BTN_GAP,       8
.equ BTN_COUNT,     7

// Colors (BGRA in little-endian: 0xAABBGGRR).
.equ COL_BG,        0xff202428
.equ COL_BORDER,    0xff808080
.equ COL_BTN_BG,    0xff3a4658
.equ COL_BTN_HI,    0xff5870a0
.equ COL_BTN_TXT,   0xffe0e8f0
.equ COL_TITLE_TXT, 0xff8090a0

#define EVENT_CAP 8

.section .user.image.header, "a"
.global panel_image_header
.type panel_image_header, %function
panel_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad panel_image_end - panel_image_header
    .quad panel_start - panel_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global panel_start
.type panel_start, %function

panel_start:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #160          // 8 events * 12 bytes + slack

    mov x0, #1
    adr x1, panel_banner
    bl common_write_cstr

    // Create panel window at bottom of screen.
    mov x0, #0                          // x
    mov x1, #PANEL_Y                    // y
    mov x2, #SCREEN_W                   // w
    mov x3, #PANEL_H                    // h
    movz x4, #0xff20, lsl #16
    movk x4, #0x2428                    // bg
    movz x5, #0xff80, lsl #16
    movk x5, #0x8080                    // border
    adr x6, panel_title
    mov x8, #SYS_WINDOW_CREATE
    svc #0
    cmp x0, #0
    b.lt panel_fail
    mov x19, x0                         // x19 = window_id

    bl panel_redraw_all

    mov x0, #1
    adr x1, panel_ready
    bl common_write_cstr

    // One-shot self-test: synthesize a click on the first button and route
    // it through the same path the event loop uses. Removed by build tag.
#ifdef PANEL_SELFTEST
    mov w26, #8                         // click x=8 (inside button 0)
    mov w27, #(BTN_Y - PANEL_Y + 4)
    bl panel_on_click
    b panel_loop
#endif

    add x20, sp, #0                     // x20 = event buffer base

panel_loop:
    mov x0, x19
    mov x1, x20
    mov x2, #EVENT_CAP
    mov x8, #SYS_WINDOW_EVENT
    svc #0
    mov x21, x0                         // n events
    cmp x21, #0
    b.le panel_yield

    mov x22, #0
1:  cmp x22, x21
    b.ge panel_yield

    // Each event is 12 bytes (3 x uint32_t).
    mov x23, x22
    mov x24, #12
    mul x23, x23, x24
    add x24, x20, x23

    ldr w25, [x24]                      // type
    ldr w26, [x24, #4]                  // data1
    ldr w27, [x24, #8]                  // data2

    cmp w25, #3                         // GUI_EVENT_MOUSE_CLICK
    b.ne 2f
    // data1 = abs x, data2 = abs y; convert to panel-local.
    sub w26, w26, #0                    // panel is at x=0
    sub w27, w27, #PANEL_Y
    bl panel_on_click
    b 9f

2:
    cmp w25, #4                         // GUI_EVENT_MOUSE_MOVE
    b.ne 9f
    sub w26, w26, #0
    sub w27, w27, #PANEL_Y
    bl panel_on_move

9:  add x22, x22, #1
    b 1b

panel_yield:
    mov x0, #0
    mov x8, #SYS_YIELD
    svc #0
    b panel_loop

panel_fail:
    mov x0, #1
    adr x1, panel_fail_msg
    bl common_write_cstr
    mov x0, #1
    mov x8, #SYS_EXIT
    svc #0

// panel_redraw_all: redraw the panel background, top border, and all buttons.
panel_redraw_all:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    // Background fill (already part of bg, but redraw to clear prior state).
    mov x0, x19
    mov x1, #0
    mov x2, #0
    movz x3, #0xff20, lsl #16
    movk x3, #0x2428
    movz x4, #SCREEN_W & 0xffff, lsl #0
    mov x5, #PANEL_H
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    // Top border line at y=0.
    mov x0, x19
    mov x1, #0
    mov x2, #0
    mov x3, #SCREEN_W
    mov x4, #1
    movz x5, #0xff50, lsl #16
    movk x5, #0x6070
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    // Draw each button.
    mov x26, #0                         // button index
panel_btn_loop:
    cmp x26, #BTN_COUNT
    b.ge panel_btn_done

    mov x0, x26
    bl panel_button_x                    // x0 = button x
    mov x27, x0                         // x27 = x

    mov x0, x19
    mov x1, x27
    mov x2, #BTN_Y - PANEL_Y
    mov x3, #BTN_W
    mov x4, #BTN_H
    movz x5, #0xff3a, lsl #16
    movk x5, #0x4658
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    // Button label (centered roughly).
    mov x0, x19
    mov x1, x27
    add x1, x1, #8
    mov x2, #BTN_Y - PANEL_Y
    add x2, x2, #6
    movz x3, #0xffe0, lsl #16
    movk x3, #0xe8f0
    mov x0, x26
    bl panel_label_ptr
    mov x4, x0
    mov x0, x19
    mov x1, x1
    mov x2, x2
    mov x3, x3
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0

    add x26, x26, #1
    b panel_btn_loop
panel_btn_done:

    // Trigger redraw.
    mov x0, x19
    mov x8, #SYS_WINDOW_REDRAW
    svc #0

    ldp x29, x30, [sp], #16
    ret

// panel_button_x: x0 = index in, x0 = button x (panel-local).
panel_button_x:
    mov x1, #BTN_W
    add x1, x1, #BTN_GAP
    mul x0, x0, x1
    add x0, x0, #4
    ret

// panel_label_ptr: x0 = index, x0 = pointer to label string.
panel_label_ptr:
    adr x1, panel_labels
    mov x2, #10
    mul x0, x0, x2
    add x0, x0, x1
    ret

// panel_on_click: w26 = panel-local x, w27 = panel-local y.
// Determine which button was clicked and call sys_spawn for that app.
panel_on_click:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    cmp w27, #0
    b.lt panel_on_click_done
    cmp w27, #(BTN_Y - PANEL_Y + BTN_H)
    b.ge panel_on_click_done

    mov x0, #BTN_W
    add x0, x0, #BTN_GAP
    udiv x1, x26, x0                    // x1 = button index
    msub x2, x1, x0, x26                // remainder
    cmp x2, #BTN_W
    b.ge panel_on_click_done
    cmp x1, #BTN_COUNT
    b.ge panel_on_click_done

    mov x9, x1                          // x9 = button index

    // Print launch message.
    mov x11, x9
    mov x12, #10
    mul x11, x11, x12
    adr x13, panel_labels
    add x11, x11, x13                  // x11 = app name

    mov x0, #1
    adr x1, panel_launch_pre
    bl common_write_cstr
    mov x0, #1
    mov x1, x11
    bl common_write_cstr
    mov x0, #1
    adr x1, panel_launch_post
    bl common_write_cstr

    // Build path "/kolibri/<name>\0" on stack.
    sub sp, sp, #32
    mov x14, sp                         // x14 = path base (stable across writes)
    adr x0, panel_path_prefix
    mov x1, x14
    bl panel_strcpy_to                  // copies "/kolibri/" to [x1], x1 returns past end

    mov x0, x11
    mov x1, x1
    bl panel_append                     // appends app name
    mov x0, #0
    strb w0, [x1]

    mov x0, x14
    mov x1, #0
    mov x8, #SYS_SPAWN
    svc #0

    add sp, sp, #32

panel_on_click_done:
    ldp x29, x30, [sp], #16
    ret

// panel_on_move: w26 = panel-local x, w27 = panel-local y. Currently a no-op
// (no hover effect).
panel_on_move:
    ret

// panel_strcpy_to: copy x0 (null-terminated) to x1; x1 returns ptr to '\0'.
panel_strcpy_to:
    mov x2, x0
    mov x3, x1
1:  ldrb w4, [x2]
    cbz w4, 2f
    strb w4, [x3], #1
    add x2, x2, #1
    b 1b
2:  mov x0, x3                          // x0 = ptr to '\0' (return value)
    ret

// panel_append: append x0 (null-terminated) to x1; x0 returns ptr to '\0'.
panel_append:
    mov x2, x0
    mov x3, x1
1:  ldrb w4, [x2]
    cbz w4, 2f
    strb w4, [x3], #1
    add x2, x2, #1
    b 1b
2:  mov x0, x3
    ret

.section .user.image.rodata, "a"
panel_title:
    .ascii "panel\0"
panel_banner:
    .ascii "panel: starting\n\0"
panel_ready:
    .ascii "panel: ready\n\0"
panel_fail_msg:
    .ascii "panel: window create failed\n\0"
panel_launch_pre:
    .ascii "panel: launch \0"
panel_launch_post:
    .ascii "\n\0"
panel_path_prefix:
    .ascii "/kolibri/\0"

// One 10-byte slot per app, null-padded. Must match BTN_COUNT and order.
panel_labels:
    .ascii "hello\0\0\0\0\0"
    .ascii "loop\0\0\0\0\0\0"
    .ascii "fault\0\0\0\0\0"
    .ascii "shell\0\0\0\0\0"
    .ascii "editor\0\0\0\0"
    .ascii "monitor\0\0\0\0"
    .ascii "win\0\0\0\0\0\0\0"
panel_image_end:
