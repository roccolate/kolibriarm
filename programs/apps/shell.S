// KolibriARM app: shell
//
// Minimal windowed command shell. It reads key events from its GUI window,
// keeps a tiny fixed line history, and supports a small command set:
// help, ls, mem, ps, ticks, run <name> [args...], kill last, and exit.
// The `run` command splits its tail on whitespace and passes the
// resulting tokens to the spawned app through SYS_SPAWN_ARGV so each
// arg lands in the new process's argv[] on its initial stack.

#define SYS_EXIT         1
#define SYS_YIELD        2
#define SYS_SPAWN        4
#define SYS_SPAWN_ARGV   8
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

// Synthetic key codes delivered through the GUI event queue when the
// user presses arrow keys in a real terminal attached to the QEMU
// serial console. See drivers/input/input.h.
#define INPUT_KEY_UP    0x101
#define INPUT_KEY_DOWN  0x102

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
// shell_argv: the argv[] pointer array plus per-token string buffers.
// We hold up to 8 tokens (matches the kernel USER_DEMO_ARGV_MAX_STRINGS
// cap) and the longest token can be LINE_CAP-1 bytes.
.equ ARGV_MAX,       8
.equ HISTORY_DEPTH,  8
.equ OFF_LINE,       0
.equ OFF_EVENTS,     64
.equ OFF_LINES,      160
.equ OFF_TEMP,       544
.equ OFF_INFO,       608
.equ OFF_PROCS,      640
// shell_history: a fixed-depth ring of the most recent non-empty
// input lines. UP/DOWN navigation copies one entry into OFF_LINE so
// the user can re-run or edit a previous command without retyping.
.equ OFF_HISTORY,        (OFF_PROCS + PROC_CAP * 24)  // 728
.equ OFF_HISTORY_COUNT,  (OFF_HISTORY + HISTORY_DEPTH * LINE_CAP)  // 1240
.equ OFF_HISTORY_CURSOR, (OFF_HISTORY_COUNT + 8)      // 1248
// shell_argv layout: argv_ptrs[ARGV_MAX] then argv_strs[ARGV_MAX][LINE_CAP].
.equ OFF_ARGV_PTRS,  (OFF_HISTORY_CURSOR + 8)        // 1256
.equ OFF_ARGV_STRS,  (OFF_ARGV_PTRS + ARGV_MAX * 8)   // 1320
.equ FRAME_BYTES,    (OFF_ARGV_STRS + ARGV_MAX * LINE_CAP)   // 1832

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

    // Initialise command history: zero entries, no navigation.
    mov x9, sp
    add x9, x9, #OFF_HISTORY_COUNT
    str xzr, [x9]
    add x9, x9, #8
    str xzr, [x9]                     // history_cursor == history_count

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
    cmp x0, #INPUT_KEY_UP
    b.eq shell_history_up
    cmp x0, #INPUT_KEY_DOWN
    b.eq shell_history_down
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

// shell_history_copy: copy the cstring at src (up to LINE_CAP-1 bytes)
// into the current line buffer. Updates x21 (current input length).
shell_history_copy:
    mov x9, x0
    mov x10, #0
1:
    cmp x10, #(LINE_CAP - 1)
    b.ge 2f
    ldrb w11, [x9, x10]
    cbz w11, 2f
    strb w11, [x20, x10]
    add x10, x10, #1
    b 1b
2:
    mov w11, #0
    strb w11, [x20, x10]
    mov x21, x10
    ret

// shell_history_push: append the current non-empty line to the history
// buffer, shifting older entries up when the buffer is full.
shell_history_push:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    cbz x21, shell_history_push_done
    add x9, x29, #16
    ldr x10, [x9, #OFF_HISTORY_COUNT]
    cmp x10, #HISTORY_DEPTH
    b.lt 2f

    // Buffer full: shift entries [0..N-2] up by one slot.
    mov x11, #0
1:
    cmp x11, x10
    b.ge 2f
    add x12, x11, #1
    add x13, x9, #OFF_HISTORY
    add x13, x13, x11, lsl #6         // x13 = &history[i]
    add x14, x13, #64                // x14 = &history[i+1]
    mov x15, #0
    mov x16, #63                      // LINE_CAP - 1 + NUL
    ldrb w17, [x14, x15]
    strb w17, [x13, x15]
    add x15, x15, #1
    cmp x15, x16
    b.lt 4f
4:
    mov x11, x12
    b 1b
2:
    add x10, x10, #1
    str x10, [x9, #OFF_HISTORY_COUNT]

    // Copy current line into history[count-1].
    sub x10, x10, #1
    add x11, x9, #OFF_HISTORY
    add x11, x11, x10, lsl #6
    mov x12, #0
3:
    cmp x12, #(LINE_CAP - 1)
    b.ge shell_history_push_done
    ldrb w13, [x20, x12]
    cbz w13, shell_history_push_done
    strb w13, [x11, x12]
    add x12, x12, #1
    b 3b

shell_history_push_done:
    // After pushing, cursor sits at the (new) end so further UP presses
    // pull older entries.
    add x9, x29, #16
    ldr x10, [x9, #OFF_HISTORY_COUNT]
    str x10, [x9, #OFF_HISTORY_CURSOR]
    ldp x29, x30, [sp], #16
    ret

// shell_history_up: walk one slot older in the history (cursor--).
// No-op if already at the oldest entry.
shell_history_up:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    add x9, x29, #16
    ldr x10, [x9, #OFF_HISTORY_CURSOR]
    cbz x10, shell_history_up_done
    sub x10, x10, #1
    str x10, [x9, #OFF_HISTORY_CURSOR]
    add x11, x9, #OFF_HISTORY
    add x0, x11, x10, lsl #6
    bl shell_history_copy
shell_history_up_done:
    ldp x29, x30, [sp], #16
    ret

// shell_history_down: walk one slot newer in the history (cursor++).
// When the cursor runs past the newest entry the line is cleared.
shell_history_down:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    add x9, x29, #16
    ldr x10, [x9, #OFF_HISTORY_CURSOR]
    ldr x11, [x9, #OFF_HISTORY_COUNT]
    cmp x10, x11
    b.ge shell_history_down_done
    add x10, x10, #1
    str x10, [x9, #OFF_HISTORY_CURSOR]
    cmp x10, x11
    b.ne 1f
    // Past the newest entry: clear the line.
    mov w12, #0
    strb w12, [x20]
    mov x21, #0
    b shell_history_down_done
1:
    add x11, x9, #OFF_HISTORY
    add x0, x11, x10, lsl #6
    bl shell_history_copy
shell_history_down_done:
    ldp x29, x30, [sp], #16
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
    bl shell_history_push              // capture for UP recall
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

// shell_cmd_run: parse "run X Y Z" and spawn X with argv = ["X","Y","Z"].
// The first whitespace-delimited token is the app name; remaining tokens
// are passed as argv to the spawned process via SYS_SPAWN_ARGV. argv_ptrs
// point at per-token string buffers in OFF_ARGV_STRS so the kernel has
// stable memory to copy onto the new stack.
shell_cmd_run:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    // Skip "run " prefix; x9 points at the first token (the app name).
    add x9, x20, #4
    ldrb w0, [x9]
    cbz w0, shell_cmd_run_fail

    // Reset argv storage so we can detect overflow.
    mov x10, #0
    mov x11, sp
    add x11, x11, #16 + OFF_ARGV_PTRS
    mov x12, x11
shell_run_clear_loop:
    cmp x10, #ARGV_MAX
    b.ge shell_run_clear_done
    str xzr, [x12], #8
    add x10, x10, #1
    b shell_run_clear_loop
shell_run_clear_done:
    mov x14, #0                      // x14 = argc

    // Build the path "/kolibri/<argv[0]>" in x24.
    mov x15, x24
    adr x16, shell_kolibri_prefix
    mov x17, #0
shell_run_path_loop:
    ldrb w0, [x16, x17]
    cbz w0, shell_run_path_done
    strb w0, [x15, x17]
    add x17, x17, #1
    b shell_run_path_loop
shell_run_path_done:

    // Tokenise the input starting at x9. x9 scans the input; x10
    // indexes argv[]; x18 points at the destination token buffer.
shell_run_tok_outer:
    ldrb w0, [x9]
    cbz w0, shell_run_tok_done      // end of input
    cmp w0, #' '
    b.ne shell_run_tok_start
    add x9, x9, #1                  // skip inter-token whitespace
    b shell_run_tok_outer

shell_run_tok_start:
    cmp x14, #ARGV_MAX
    b.ge shell_run_tok_done          // too many tokens

    // Reserve argv[i] = pointer to token buffer.
    mov x18, sp
    add x18, x18, #16 + OFF_ARGV_STRS
    mov x19, #LINE_CAP
    mul x19, x19, x14
    add x18, x18, x19

    mov x11, sp
    add x11, x11, #16 + OFF_ARGV_PTRS
    mov x12, #8
    mul x12, x12, x14
    add x11, x11, x12
    str x18, [x11]

    // Copy the token bytes until whitespace or NUL.
    mov x13, #0
shell_run_tok_copy:
    ldrb w0, [x9]
    cbz w0, shell_run_tok_end
    cmp w0, #' '
    b.eq shell_run_tok_end
    cmp x13, #(LINE_CAP - 1)
    b.ge shell_run_tok_end
    strb w0, [x18, x13]
    add x13, x13, #1
    add x9, x9, #1
    b shell_run_tok_copy
shell_run_tok_end:
    mov w0, #0
    strb w0, [x18, x13]
    cbz x13, shell_run_tok_done       // empty token, ignore

    // If this is the first token, also append it to the path buffer.
    cmp x14, #0
    b.ne 1f
    add x10, x17, #0                 // x10 = current path length
1:
    cmp x14, #0
    b.ne 2f
    mov x11, #0
shell_run_path_append:
    ldrb w0, [x18, x11]
    cbz w0, 2f
    cmp x10, #(DISPLAY_COLS - 1)
    b.ge 2f
    strb w0, [x15, x10]
    add x10, x10, #1
    add x11, x11, #1
    b shell_run_path_append
2:
    add x14, x14, #1                // argc++

    // Skip trailing whitespace before the next token.
shell_run_tok_skip:
    ldrb w0, [x9]
    cbz w0, shell_run_tok_done
    cmp w0, #' '
    b.ne shell_run_tok_done
    add x9, x9, #1
    b shell_run_tok_skip

shell_run_tok_done:
    mov w0, #0
    strb w0, [x15, x10]              // NUL-terminate the path
    cbz x14, shell_cmd_run_fail       // nothing parsed

    // Build argv pointer (absolute address in caller memory).
    add x0, sp, #16 + OFF_ARGV_PTRS
    mov x1, x14
    mov x2, x24                       // path
    mov x3, #0                       // entry_index
    mov x8, #SYS_SPAWN_ARGV
    svc #0
    tbnz x0, #63, shell_cmd_run_fail

    mov x26, x0
    adr x0, shell_spawned
    bl shell_push_line
    ldp x29, x30, [sp], #16
    ret

shell_cmd_run_fail:
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
