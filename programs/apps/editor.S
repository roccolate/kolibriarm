// KolibriARM app: editor
//
// Minimal GUI editor for /tmp/note. It owns a window, displays the first
// screenful of the note, appends printable key presses to the buffer,
// supports backspace/newline, saves with Ctrl-S, and exits with Ctrl-Q or the
// title-bar close button.

#define SYS_EXIT         1
#define SYS_YIELD        2
#define SYS_OPEN        40
#define SYS_CLOSE       41
#define SYS_READ        42
#define SYS_WRITE       43
#define SYS_SEEK        44
#define SYS_WINDOW_CREATE    70
#define SYS_WINDOW_DESTROY   71
#define SYS_WINDOW_DRAW_TEXT 72
#define SYS_WINDOW_DRAW_RECT 73
#define SYS_WINDOW_EVENT     74
#define SYS_WINDOW_SET_TITLE 75
#define SYS_WINDOW_REDRAW    76

.equ O_RDONLY,       0
.equ O_RDWR,         2
.equ WIN_X,          96
.equ WIN_Y,          72
.equ WIN_W,          420
.equ WIN_H,          260
.equ TITLE_BAR_H,    12
.equ FILE_CAP,       512
.equ RENDER_CAP,     128
.equ EVENT_CAP,      8
.equ OFF_FILE,       0
.equ OFF_RENDER,     512
.equ OFF_EVENTS,     640
.equ FRAME_BYTES,    768

.section .user.image.header, "a"
.global editor_image_header
.type editor_image_header, %function
editor_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad editor_image_end - editor_image_header
    .quad editor_start - editor_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global editor_start
.type editor_start, %function

editor_start:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #FRAME_BYTES

    add x20, sp, #OFF_FILE           // x20 = file buffer
    add x22, sp, #OFF_RENDER         // x22 = render buffer
    add x23, sp, #OFF_EVENTS         // x23 = event buffer
    mov x21, #0                      // x21 = file length

    mov x0, #1
    adr x1, editor_banner
    bl common_write_cstr

    bl editor_load_note
    bl editor_create_window
    bl editor_redraw

editor_loop:
    mov x0, x19
    mov x1, x23
    mov x2, #EVENT_CAP
    mov x8, #SYS_WINDOW_EVENT
    svc #0
    cmp x0, #0
    b.le editor_loop_yield

    mov x24, x0                      // x24 = event count
    mov x25, #0
1:
    cmp x25, x24
    b.ge editor_loop

    mov x26, #12
    mul x26, x25, x26
    add x27, x23, x26                // &events[i]
    ldr w26, [x27]                   // type
    ldr w27, [x27, #4]               // data1

    cmp w26, #6                      // GUI_EVENT_CLOSE
    b.eq editor_close
    cmp w26, #1                      // GUI_EVENT_KEY_PRESS
    b.ne 2f
    mov x0, x27
    bl editor_handle_key
2:
    add x25, x25, #1
    b 1b

editor_loop_yield:
    mov x0, #0
    mov x8, #SYS_YIELD
    svc #0
    b editor_loop

editor_close:
    mov x0, x19
    mov x8, #SYS_WINDOW_DESTROY
    svc #0
    add sp, sp, #FRAME_BYTES
    ldp x29, x30, [sp], #16
    mov x0, #0
    mov x8, #SYS_EXIT
    svc #0

editor_fail:
    mov x0, #1
    adr x1, editor_fail_msg
    bl common_write_cstr
    add sp, sp, #FRAME_BYTES
    ldp x29, x30, [sp], #16
    mov x0, #1
    mov x8, #SYS_EXIT
    svc #0

// editor_load_note: best-effort load of /tmp/note into x20, length in x21.
editor_load_note:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    adr x0, editor_path
    mov x1, #O_RDONLY
    mov x8, #SYS_OPEN
    svc #0
    tbnz x0, #63, 9f
    mov x24, x0

    mov x0, x24
    mov x1, x20
    mov x2, #(FILE_CAP - 1)
    mov x8, #SYS_READ
    svc #0
    tbnz x0, #63, 1f
    mov x21, x0
    mov w25, #0
    strb w25, [x20, x21]

1:
    mov x0, x24
    mov x8, #SYS_CLOSE
    svc #0
9:
    ldp x29, x30, [sp], #16
    ret

editor_create_window:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    mov x0, #WIN_X
    mov x1, #WIN_Y
    mov x2, #WIN_W
    mov x3, #WIN_H
    movz x4, #0xff20, lsl #16
    movk x4, #0x2830
    movz x5, #0xff80, lsl #16
    movk x5, #0x8080
    adr x6, editor_title
    mov x8, #SYS_WINDOW_CREATE
    svc #0
    cmp x0, #0
    b.lt 1f
    mov x19, x0

    mov x0, x19
    adr x1, editor_title
    mov x2, #TITLE_BAR_H
    mov x8, #SYS_WINDOW_SET_TITLE
    svc #0

    ldp x29, x30, [sp], #16
    ret
1:
    ldp x29, x30, [sp], #16
    b editor_fail

editor_redraw:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    // Clear content area.
    mov x0, x19
    mov x1, #1
    mov x2, #0
    mov x3, #(WIN_W - 2)
    mov x4, #(WIN_H - TITLE_BAR_H - 2)
    movz x5, #0xff20, lsl #16
    movk x5, #0x2830
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    mov x0, x19
    mov x1, #12
    mov x2, #8
    movz x3, #0xffc0, lsl #16
    movk x3, #0xd0e0
    adr x4, editor_status
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0

    bl editor_build_render

    mov x0, x19
    mov x1, #12
    mov x2, #28
    movz x3, #0xffe0, lsl #16
    movk x3, #0xe8f0
    mov x4, x22
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0

    mov x0, x19
    mov x8, #SYS_WINDOW_REDRAW
    svc #0

    ldp x29, x30, [sp], #16
    ret

// Copy the visible prefix into x22 and null-terminate for SYS_WINDOW_DRAW_TEXT.
editor_build_render:
    mov x24, #0
1:
    cmp x24, #(RENDER_CAP - 1)
    b.ge 2f
    cmp x24, x21
    b.ge 2f
    ldrb w25, [x20, x24]
    strb w25, [x22, x24]
    add x24, x24, #1
    b 1b
2:
    mov w25, #0
    strb w25, [x22, x24]
    ret

editor_handle_key:
    // x0 = key value
    cmp x0, #17                     // Ctrl-Q
    b.eq editor_close
    cmp x0, #19                     // Ctrl-S
    b.eq editor_save_and_redraw
    cmp x0, #8
    b.eq editor_backspace_key
    cmp x0, #127
    b.eq editor_backspace_key
    cmp x0, #13
    b.eq editor_newline_key
    cmp x0, #10
    b.eq editor_newline_key
    cmp x0, #32
    b.lt 9f
    cmp x0, #126
    b.gt 9f
    cmp x21, #(FILE_CAP - 1)
    b.ge 9f
    strb w0, [x20, x21]
    add x21, x21, #1
    mov w24, #0
    strb w24, [x20, x21]
    b editor_redraw
9:
    ret

editor_newline_key:
    cmp x21, #(FILE_CAP - 1)
    b.ge 9f
    mov w24, #10
    strb w24, [x20, x21]
    add x21, x21, #1
    mov w24, #0
    strb w24, [x20, x21]
    b editor_redraw
9:
    ret

editor_backspace_key:
    cbz x21, 9f
    sub x21, x21, #1
    mov w24, #0
    strb w24, [x20, x21]
    b editor_redraw
9:
    ret

editor_save_and_redraw:
    bl editor_save_note
    b editor_redraw

editor_save_note:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    adr x0, editor_path
    mov x1, #O_RDWR
    mov x8, #SYS_OPEN
    svc #0
    tbnz x0, #63, 9f
    mov x24, x0

    mov x0, x24
    mov x1, #0
    mov x2, #0
    mov x8, #SYS_SEEK
    svc #0
    tbnz x0, #63, 1f

    mov x0, x24
    mov x1, x20
    mov x2, x21
    mov x8, #SYS_WRITE
    svc #0

1:
    mov x0, x24
    mov x8, #SYS_CLOSE
    svc #0
9:
    ldp x29, x30, [sp], #16
    ret

.section .user.image.rodata, "a"
editor_title:
    .ascii "editor\0"
editor_banner:
    .ascii "editor: starting\n\0"
editor_fail_msg:
    .ascii "editor: window create failed\n\0"
editor_status:
    .ascii "CTRL S SAVE CTRL Q CLOSE\0"
editor_path:
    .asciz "/tmp/note"
editor_image_end:
