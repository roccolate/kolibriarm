// KolibriARM app: shell
//
// Minimal windowed command shell. It reads key events from its GUI window,
// keeps a tiny fixed line history, and supports a small command set:
// help, ls, mem, ps, ticks, run <name>, kill last, and exit.

#define SYS_EXIT         1
#define SYS_YIELD        2
#define SYS_SPAWN        4
#define SYS_KILL         7
#define SYS_READDIR     46
#define SYS_TIMEINFO   100
#define SYS_MEMINFO    101
#define SYS_PROCLIST   102
#define SYS_WINDOW_CREATE    70
#define SYS_WINDOW_DESTROY   71
#define SYS_WINDOW_DRAW_TEXT 72
#define SYS_WINDOW_DRAW_RECT 73
#define SYS_WINDOW_EVENT     74
#define SYS_WINDOW_SET_TITLE 75
#define SYS_WINDOW_REDRAW    76

.equ WIN_X,          72
.equ WIN_Y,          52
.equ WIN_W,          480
.equ WIN_H,          280
.equ TITLE_BAR_H,    12
.equ LINE_CAP,       64
.equ DISPLAY_LINES,  8
.equ DISPLAY_COLS,   48
.equ EVENT_CAP,      8
.equ PROC_CAP,       4
.equ OFF_LINE,       0
.equ OFF_EVENTS,     64
.equ OFF_LINES,      160
.equ OFF_TEMP,       544
.equ OFF_INFO,       608
.equ OFF_PROCS,      640
.equ FRAME_BYTES,    768

.section .user.image.header, "a"
.global shell_image_header
.type shell_image_header, %function
shell_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad shell_image_end - shell_image_header
    .quad shell_start - shell_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global shell_start
.type shell_start, %function

shell_start:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #FRAME_BYTES

    add x20, sp, #OFF_LINE           // current input line
    mov x21, #0                      // current input length
    add x22, sp, #OFF_EVENTS         // event buffer
    add x23, sp, #OFF_LINES          // display line ring, shifted in place
    add x24, sp, #OFF_TEMP           // temp text/path/number buffer
    add x25, sp, #OFF_INFO           // info syscall buffer
    add x27, sp, #OFF_PROCS          // process-list buffer
    mov x26, #0                      // last spawned pid

    bl shell_clear_display
    adr x0, shell_ready
    bl shell_push_line
    bl shell_create_window
    bl shell_redraw

shell_loop:
    mov x0, x19
    mov x1, x22
    mov x2, #EVENT_CAP
    mov x8, #SYS_WINDOW_EVENT
    svc #0
    cmp x0, #0
    b.gt shell_handle_events

    mov x0, #0
    mov x8, #SYS_YIELD
    svc #0
    b shell_loop

shell_handle_events:
    mov x28, x0                      // n events
    mov x9, #0
1:
    cmp x9, x28
    b.ge shell_after_events
    mov x10, #12
    mul x10, x9, x10
    add x11, x22, x10
    ldr w12, [x11]                   // type
    ldr w13, [x11, #4]               // data1
    cmp w12, #6                      // GUI_EVENT_CLOSE
    b.eq shell_exit
    cmp w12, #1                      // GUI_EVENT_KEY_PRESS
    b.ne 2f
    mov x0, x13
    bl shell_handle_key
    b shell_after_events
2:
    add x9, x9, #1
    b 1b

shell_after_events:
    bl shell_redraw
    b shell_loop

shell_exit:
    mov x0, x19
    mov x8, #SYS_WINDOW_DESTROY
    svc #0
    add sp, sp, #FRAME_BYTES
    ldp x29, x30, [sp], #16
    mov x0, #0
    mov x8, #SYS_EXIT
    svc #0

shell_fail:
    mov x0, #1
    adr x1, shell_fail_msg
    bl common_write_cstr
    add sp, sp, #FRAME_BYTES
    ldp x29, x30, [sp], #16
    mov x0, #1
    mov x8, #SYS_EXIT
    svc #0

shell_create_window:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    mov x0, #WIN_X
    mov x1, #WIN_Y
    mov x2, #WIN_W
    mov x3, #WIN_H
    movz x4, #0xff14, lsl #16
    movk x4, #0x1820
    movz x5, #0xff70, lsl #16
    movk x5, #0x8870
    adr x6, shell_title
    mov x8, #SYS_WINDOW_CREATE
    svc #0
    cmp x0, #0
    b.lt 1f
    mov x19, x0

    mov x0, x19
    adr x1, shell_title
    mov x2, #TITLE_BAR_H
    mov x8, #SYS_WINDOW_SET_TITLE
    svc #0

    ldp x29, x30, [sp], #16
    ret
1:
    ldp x29, x30, [sp], #16
    b shell_fail

shell_handle_key:
    cmp x0, #17                     // Ctrl-Q
    b.eq shell_exit
    cmp x0, #13
    b.eq shell_enter
    cmp x0, #10
    b.eq shell_enter
    cmp x0, #8
    b.eq shell_backspace
    cmp x0, #127
    b.eq shell_backspace
    cmp x0, #32
    b.lt 9f
    cmp x0, #126
    b.gt 9f
    cmp x21, #(LINE_CAP - 1)
    b.ge 9f
    strb w0, [x20, x21]
    add x21, x21, #1
    mov w1, #0
    strb w1, [x20, x21]
9:
    ret

shell_backspace:
    cbz x21, 9f
    sub x21, x21, #1
    mov w1, #0
    strb w1, [x20, x21]
9:
    ret

shell_enter:
    mov w1, #0
    strb w1, [x20, x21]
    cbz x21, 9f
    mov x0, x20
    bl shell_push_line
    bl shell_dispatch_command
    mov x21, #0
    strb wzr, [x20]
9:
    ret

shell_dispatch_command:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    mov x0, x20
    adr x1, shell_help_cmd
    bl common_streq
    cbnz x0, shell_cmd_help

    mov x0, x20
    adr x1, shell_ls_cmd
    bl common_streq
    cbnz x0, shell_cmd_ls

    mov x0, x20
    adr x1, shell_mem_cmd
    bl common_streq
    cbnz x0, shell_cmd_mem

    mov x0, x20
    adr x1, shell_ps_cmd
    bl common_streq
    cbnz x0, shell_cmd_ps

    mov x0, x20
    adr x1, shell_ticks_cmd
    bl common_streq
    cbnz x0, shell_cmd_ticks

    mov x0, x20
    adr x1, shell_kill_last_cmd
    bl common_streq
    cbnz x0, shell_cmd_kill_last

    mov x0, x20
    adr x1, shell_pwd_cmd
    bl common_streq
    cbnz x0, shell_cmd_pwd

    mov x0, x20
    adr x1, shell_exit_cmd
    bl common_streq
    cbnz x0, shell_cmd_exit

    ldrb w0, [x20]
    cmp w0, #'r'
    b.ne 1f
    ldrb w0, [x20, #1]
    cmp w0, #'u'
    b.ne 1f
    ldrb w0, [x20, #2]
    cmp w0, #'n'
    b.ne 1f
    ldrb w0, [x20, #3]
    cmp w0, #' '
    b.eq shell_cmd_run

1:
    adr x0, shell_unknown
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_cmd_help:
    adr x0, shell_help_msg
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_cmd_exit:
    ldp x29, x30, [sp], #16
    b shell_exit

shell_cmd_ls:
    adr x0, shell_root_path
    mov x1, x24
    mov x2, #DISPLAY_COLS
    mov x8, #SYS_READDIR
    svc #0
    tbnz x0, #63, 1f
    mov x9, x0
    cmp x9, #(DISPLAY_COLS - 1)
    b.le 2f
    mov x9, #(DISPLAY_COLS - 1)
2:
    mov w10, #0
    strb w10, [x24, x9]
    mov x0, x24
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret
1:
    adr x0, shell_ls_error
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_cmd_mem:
    mov x0, x25
    mov x8, #SYS_MEMINFO
    svc #0
    tbnz x0, #63, 1f
    adr x0, shell_free_msg
    bl shell_push_line
    ldr x0, [x25, #8]
    mov x1, x24
    bl shell_format_dec
    mov x0, x24
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret
1:
    adr x0, shell_mem_error
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_cmd_ps:
    mov x0, x27
    mov x1, #PROC_CAP
    mov x8, #SYS_PROCLIST
    svc #0
    tbnz x0, #63, 9f
    mov x28, x0
    mov x16, #0
    mov x17, x27
    adr x0, shell_ps_header
    bl shell_push_line
1:
    cmp x16, x28
    b.ge 8f
    add x0, x17, #8
    bl shell_push_line
    add x17, x17, #24
    add x16, x16, #1
    b 1b
8:
    ldp x29, x30, [sp], #16
    ret
9:
    adr x0, shell_ps_error
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_cmd_ticks:
    mov x0, x25
    mov x8, #SYS_TIMEINFO
    svc #0
    tbnz x0, #63, 1f
    adr x0, shell_ticks_msg
    bl shell_push_line
    ldr x0, [x25]
    mov x1, x24
    bl shell_format_dec
    mov x0, x24
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret
1:
    adr x0, shell_ticks_error
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_cmd_run:
    add x9, x20, #4                 // app name
    ldrb w0, [x9]
    cbz w0, 9f

    mov x10, x24
    adr x11, shell_kolibri_prefix
    mov x12, #0
1:
    ldrb w0, [x11, x12]
    cbz w0, 2f
    strb w0, [x10, x12]
    add x12, x12, #1
    b 1b
2:
    mov x13, #0
3:
    cmp x12, #(DISPLAY_COLS - 1)
    b.ge 9f
    ldrb w0, [x9, x13]
    cbz w0, 4f
    cmp w0, #' '
    b.eq 4f
    strb w0, [x10, x12]
    add x12, x12, #1
    add x13, x13, #1
    b 3b
4:
    mov w0, #0
    strb w0, [x10, x12]
    cmp x13, #0
    b.eq 9f

    mov x0, x24
    mov x8, #SYS_SPAWN
    svc #0
    tbnz x0, #63, 9f
    mov x26, x0
    adr x0, shell_spawned
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret
9:
    adr x0, shell_run_error
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_cmd_kill_last:
    cbz x26, 1f
    mov x0, x26
    mov x8, #SYS_KILL
    svc #0
    mov x26, #0
    adr x0, shell_killed
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret
1:
    adr x0, shell_kill_error
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_cmd_pwd:
    adr x0, shell_pwd_msg
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_redraw:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    mov x0, x19
    mov x1, #1
    mov x2, #0
    mov x3, #(WIN_W - 2)
    mov x4, #(WIN_H - TITLE_BAR_H - 2)
    movz x5, #0xff14, lsl #16
    movk x5, #0x1820
    mov x8, #SYS_WINDOW_DRAW_RECT
    svc #0

    mov x1, #12
    mov x2, #8
    adr x4, shell_heading
    bl shell_draw_text

    mov x9, #0
    mov x10, #28
1:
    cmp x9, #DISPLAY_LINES
    b.ge 2f
    mov x11, #DISPLAY_COLS
    mul x12, x9, x11
    add x4, x23, x12
    mov x1, #12
    mov x2, x10
    bl shell_draw_text
    add x9, x9, #1
    add x10, x10, #16
    b 1b

2:
    mov x1, #12
    mov x2, #168
    adr x4, shell_prompt_label
    bl shell_draw_text
    mov x1, #32
    mov x2, #168
    mov x4, x20
    bl shell_draw_text
    mov x1, #12
    mov x2, #196
    adr x4, shell_footer
    bl shell_draw_text

    mov x0, x19
    mov x8, #SYS_WINDOW_REDRAW
    svc #0

    ldp x29, x30, [sp], #16
    ret

// shell_draw_text: x1=x, x2=y, x4=cstr.
shell_draw_text:
    mov x0, x19
    movz x3, #0xffd8, lsl #16
    movk x3, #0xe8d8
    mov x8, #SYS_WINDOW_DRAW_TEXT
    svc #0
    ret

shell_clear_display:
    mov x9, #0
    mov x10, #(DISPLAY_LINES * DISPLAY_COLS)
1:
    cmp x9, x10
    b.ge 2f
    strb wzr, [x23, x9]
    add x9, x9, #1
    b 1b
2:
    ret

// shell_push_line: x0=cstr. Shifts display lines up and copies into last row.
shell_push_line:
    mov x9, #0
1:
    cmp x9, #(DISPLAY_LINES - 1)
    b.ge 4f
    mov x10, #DISPLAY_COLS
    mul x11, x9, x10
    add x12, x23, x11              // dst
    add x13, x12, #DISPLAY_COLS    // src = next row
    mov x14, #0
2:
    cmp x14, #DISPLAY_COLS
    b.ge 3f
    ldrb w15, [x13, x14]
    strb w15, [x12, x14]
    add x14, x14, #1
    b 2b
3:
    add x9, x9, #1
    b 1b

4:
    mov x10, #(DISPLAY_LINES - 1)
    mov x11, #DISPLAY_COLS
    mul x10, x10, x11
    add x12, x23, x10
    mov x13, #0
5:
    cmp x13, #(DISPLAY_COLS - 1)
    b.ge 6f
    ldrb w14, [x0, x13]
    cbz w14, 6f
    strb w14, [x12, x13]
    add x13, x13, #1
    b 5b
6:
    mov w14, #0
    strb w14, [x12, x13]
    add x13, x13, #1
7:
    cmp x13, #DISPLAY_COLS
    b.ge 8f
    strb wzr, [x12, x13]
    add x13, x13, #1
    b 7b
8:
    ret

// shell_format_dec: x0=value, x1=buffer. Writes a null-terminated decimal.
shell_format_dec:
    mov x2, x1
    cbnz x0, 1f
    mov w3, #'0'
    strb w3, [x2]
    strb wzr, [x2, #1]
    ret
1:
    mov x3, #0
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
    strb wzr, [x2, x3]
    ret

.section .user.image.rodata, "a"
shell_title:
    .ascii "shell\0"
shell_fail_msg:
    .ascii "shell: window create failed\n\0"
shell_ready:
    .ascii "SHELL READY\0"
shell_heading:
    .ascii "USER SHELL\0"
shell_prompt_label:
    .ascii "U\0"
shell_footer:
    .ascii "ENTER RUNS COMMAND\0"
shell_help_cmd:
    .asciz "help"
shell_ls_cmd:
    .asciz "ls"
shell_mem_cmd:
    .asciz "mem"
shell_ps_cmd:
    .asciz "ps"
shell_ticks_cmd:
    .asciz "ticks"
shell_kill_last_cmd:
    .asciz "kill last"
shell_pwd_cmd:
    .asciz "pwd"
shell_exit_cmd:
    .asciz "exit"
shell_help_msg:
    .ascii "HELP LS MEM PS TICKS RUN NAME KILL LAST EXIT\0"
shell_unknown:
    .ascii "UNKNOWN COMMAND\0"
shell_ls_error:
    .ascii "LS FAILED\0"
shell_mem_error:
    .ascii "MEM FAILED\0"
shell_ps_error:
    .ascii "PS FAILED\0"
shell_ticks_error:
    .ascii "TICKS FAILED\0"
shell_run_error:
    .ascii "RUN FAILED\0"
shell_kill_error:
    .ascii "KILL FAILED\0"
shell_free_msg:
    .ascii "FREE PAGES\0"
shell_ticks_msg:
    .ascii "TIMER TICKS\0"
shell_ps_header:
    .ascii "PROCESSES\0"
shell_spawned:
    .ascii "SPAWNED\0"
shell_killed:
    .ascii "KILLED\0"
shell_pwd_msg:
    .ascii "ROOT\0"
shell_root_path:
    .asciz "/"
shell_kolibri_prefix:
    .asciz "/kolibri/"
shell_image_end:
