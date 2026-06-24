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
.equ EVENT_CAP,       8
.equ OFF_FILE,       0
.equ OFF_RENDER,     512
.equ OFF_EVENTS,     640
.equ OFF_ARGC,       768
.equ OFF_ARGV,       776
.equ OFF_PATH,       784    // 16-byte path buffer for argv[1] override
.equ FRAME_BYTES,    800

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

    // Persist argv on the stack so callees (which can trash x0-x7)
    // can still redraw it. The shell sets these when launching
    // `run editor <file>`; the kernel places x0=argc, x1=argv at
    // first EL0 entry.
    add x28, sp, #OFF_ARGC
    str x0, [x28]                    // argc
    str x1, [x28, #8]                // argv pointer

    mov x0, #1
    adr x1, editor_banner
    bl common_write_cstr
    bl editor_print_argv             // show what the shell passed

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

// editor_resolve_path: pick the file path the editor should read/write.
// If argv[1] exists, use it (the shell passed a path). Otherwise fall
// back to the default /tmp/note (the legacy behaviour). Returns the
// resolved path pointer in x0; the buffer lives in OFF_PATH.
editor_resolve_path:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    add x28, x29, #16
    ldr x0, [x28, #OFF_ARGC]
    cmp x0, #2
    b.lt editor_resolve_default

    ldr x1, [x28, #OFF_ARGV]
    ldr x2, [x1, #8]                 // x2 = argv[1]
    cbz x2, editor_resolve_default

    // Copy argv[1] (up to 15 chars + NUL) into OFF_PATH.
    add x3, x29, #16
    add x3, x3, #OFF_PATH
    mov x4, #0
1:
    cmp x4, #15
    b.ge 2f
    ldrb w0, [x2, x4]
    cbz w0, 2f
    strb w0, [x3, x4]
    add x4, x4, #1
    b 1b
2:
    mov w0, #0
    strb w0, [x3, x4]

    add x0, x29, #16
    add x0, x0, #OFF_PATH
    ldp x29, x30, [sp], #16
    ret

editor_resolve_default:
    add x0, x29, #16
    add x0, x0, #OFF_PATH
    adr x1, editor_default_path
    mov x2, #0
3:
    ldrb w3, [x1, x2]
    strb w3, [x0, x2]
    add x2, x2, #1
    cbnz w3, 3b
    ldp x29, x30, [sp], #16
    ret

// editor_load_note: best-effort load of the resolved path into x20.
editor_load_note:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    bl editor_resolve_path
    mov x26, x0                     // x26 = resolved path

    mov x0, x26
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

    // If the shell passed args, draw "ARGV: a b c" on a second line so
    // the Phase 10.4 exit criterion ("shell correctly passes args to
    // spawned apps") is visible without needing a debugger.
    add x28, x29, #16
    ldr x0, [x28, #OFF_ARGC]
    cmp x0, #1
    b.le 1f
    bl editor_render_argv_line
1:

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

// editor_render_argv_line: walk the argv[] the shell passed and emit
// "ARGV: <tok0> <tok1> ..." into the render buffer at x22, then draw
// it under the status line. argv is reloaded from the frame before
// each window draw because SVC may trash x0-x7.
editor_render_argv_line:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    // Rebuild the render buffer from scratch with "ARGV: ..." prefix.
    mov x19, #0
1:
    cmp x19, #RENDER_CAP
    b.ge 2f
    strb wzr, [x22, x19]
    add x19, x19, #1
    b 1b
2:
    mov x20, #0                      // x20 = render index
    adr x21, editor_argv_prefix
    mov x27, #0
3:
    ldrb w0, [x21, x27]
    cbz w0, 4f
    cmp x20, #(RENDER_CAP - 1)
    b.ge 4f
    strb w0, [x22, x20]
    add x20, x20, #1
    add x27, x27, #1
    b 3b

4:
    add x28, x29, #16
    ldr x23, [x28, #OFF_ARGC]       // x23 = argc
    ldr x24, [x28, #OFF_ARGV]       // x24 = argv
    mov x25, #0                      // x25 = i

5:
    cmp x25, x23
    b.ge 9f

    cmp x20, #(RENDER_CAP - 1)
    b.ge 9f
    mov w0, #' '
    strb w0, [x22, x20]
    add x20, x20, #1

    ldr x26, [x24, x25, lsl #3]     // x26 = argv[i] string pointer
    mov x27, #0
6:
    ldrb w0, [x26, x27]
    cbz w0, 7f
    cmp x20, #(RENDER_CAP - 1)
    b.ge 7f
    strb w0, [x22, x20]
    add x20, x20, #1
    add x27, x27, #1
    b 6b
7:
    add x25, x25, #1
    b 5b

9:
    mov w0, #0
    strb w0, [x22, x20]

    mov x0, x19
    mov x1, #12
    mov x2, #22
    movz x3, #0xffc0, lsl #16
    movk x3, #0xd0e0
    mov x4, x22
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0

    ldp x29, x30, [sp], #16
    ret

// editor_print_argv: dump the argv the shell passed to stdout so the
// Phase 10.4 "shell correctly passes args to spawned apps" criterion
// is visible in the k> serial console even without a window. argc
// and argv are reloaded from the frame after every syscall because
// the SVC return path may clobber x0-x7.
editor_print_argv:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    add x28, x29, #16
    ldr x0, [x28, #OFF_ARGC]
    cmp x0, #0
    b.le 9f

    mov x0, #1
    adr x1, editor_argv_banner
    bl common_write_cstr

    add x28, x29, #16
    ldr x19, [x28, #OFF_ARGC]       // x19 = argc (callee-saved)
    ldr x20, [x28, #OFF_ARGV]       // x20 = argv
    mov x21, #0                      // x21 = i

1:
    cmp x21, x19
    b.ge 9f

    cmp x21, #0
    b.eq 2f
    mov x0, #1
    adr x1, editor_argv_sep
    bl common_write_cstr
2:
    ldr x22, [x20, x21, lsl #3]
    mov x0, #1
    mov x1, x22
    bl common_write_cstr
    add x21, x21, #1
    b 1b

9:
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

    bl editor_resolve_path
    mov x0, x0
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
editor_argv_prefix:
    .ascii "ARGV: \0"
editor_argv_banner:
    .ascii "editor: argv=\0"
editor_argv_sep:
    .ascii " \0"
editor_default_path:
    .asciz "/tmp/note"
editor_image_end:
