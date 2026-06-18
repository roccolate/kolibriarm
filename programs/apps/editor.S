// KolibriARM app: editor
//
// One-file line editor for /tmp/note. Reads the file, lets the user append
// lines, prints the buffer with .p, writes it back with .w, and quits with
// .q. The kernel still restricts writes to preallocated FAT32 files, so the
// editor can only grow /tmp/note up to the buffer size (512 bytes).

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
    sub sp, sp, #768
    mov x19, sp                   // file buffer (512 bytes)
    add x20, sp, #512             // line buffer (64 bytes at offset 512)
    mov x21, #0                   // file length

    mov x0, #1
    adr x1, editor_banner
    mov x2, editor_banner_len
    bl common_write

    adr x0, editor_path
    mov x1, #0                    // O_RDONLY
    mov x8, #40                   // sys_open
    svc #0
    tbnz x0, #63, 70f
    mov x22, x0
    mov x0, x22
    mov x1, x19
    mov x2, #512
    mov x8, #42                   // sys_read
    svc #0
    tbnz x0, #63, 70f
    mov x21, x0
    mov x0, x22
    mov x8, #41                   // sys_close
    svc #0
    b 71f

70:
    mov x0, #1
    adr x1, editor_open_error
    mov x2, editor_open_error_len
    bl common_write
    add sp, sp, #768
    mov x0, #1
    mov x8, #1                    // sys_exit
    svc #0

71:
    mov x0, #1
    adr x1, editor_loaded
    mov x2, editor_loaded_len
    bl common_write
    mov x0, x21
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, editor_newline
    mov x2, editor_newline_len
    bl common_write

72:
    mov x23, #0                   // line length

    mov x0, #1
    adr x1, editor_prompt
    mov x2, editor_prompt_len
    bl common_write

73:
    mov x0, #0
    add x1, sp, #752
    mov x2, #1
    mov x8, #42
    svc #0
    cmp x0, #1
    b.eq 74f
    mov x8, #2
    svc #0
    b 73b

74:
    ldrb w24, [sp, #752]
    cmp w24, #13
    b.eq 76f
    cmp w24, #10
    b.eq 76f
    cmp w24, #8
    b.eq 75f
    cmp w24, #127
    b.eq 75f
    cmp w24, #32
    b.lo 73b
    cmp w24, #126
    b.hi 73b
    cmp x23, #63
    b.hs 73b
    strb w24, [x20, x23]
    add x23, x23, #1
    mov x0, #1
    add x1, sp, #752
    mov x2, #1
    bl common_write
    b 73b

75:
    cbz x23, 73b
    sub x23, x23, #1
    mov x0, #1
    adr x1, editor_backspace
    mov x2, editor_backspace_len
    bl common_write
    b 73b

76:
    mov w24, #0
    strb w24, [x20, x23]
    mov x0, #1
    adr x1, editor_newline
    mov x2, editor_newline_len
    bl common_write

    mov x0, x20
    adr x1, editor_quit_cmd
    bl common_streq
    cbnz x0, 80f
    mov x0, x20
    adr x1, editor_print_cmd
    bl common_streq
    cbnz x0, 77f
    mov x0, x20
    adr x1, editor_write_cmd
    bl common_streq
    cbnz x0, 78f

    // append the new line to the file buffer
    add x25, x21, x23
    add x25, x25, #1
    cmp x25, #512
    b.hi 79f
    mov x26, #0
81:
    cmp x26, x23
    b.hs 82f
    ldrb w24, [x20, x26]
    strb w24, [x19, x21]
    add x21, x21, #1
    add x26, x26, #1
    b 81b
82:
    mov w24, #10
    strb w24, [x19, x21]
    add x21, x21, #1
    b 72b

// .p
77:
    mov x0, #1
    mov x1, x19
    mov x2, x21
    bl common_write
    b 72b

// .w
78:
    adr x0, editor_path
    mov x1, #2                    // O_RDWR
    mov x8, #40
    svc #0
    tbnz x0, #63, 79f
    mov x22, x0
    mov x0, x22
    mov x1, #0
    mov x2, #0
    mov x8, #44                   // sys_seek
    svc #0
    tbnz x0, #63, 79f
    mov x0, x22
    mov x1, x19
    mov x2, x21
    mov x8, #43                   // sys_write
    svc #0
    tbnz x0, #63, 79f
    mov x0, x22
    mov x8, #41
    svc #0
    mov x0, #1
    adr x1, editor_saved
    mov x2, editor_saved_len
    bl common_write
    b 72b

79:
    mov x0, #1
    adr x1, editor_error
    mov x2, editor_error_len
    bl common_write
    b 72b

// .q
80:
    add sp, sp, #768
    mov x0, #0
    mov x8, #1
    svc #0

.section .user.image.rodata, "a"
editor_banner:
    .ascii "editor: type, enter to append, .p print, .w write, .q quit\n"
editor_banner_len = . - editor_banner
editor_open_error:
    .ascii "editor: open failed\n"
editor_open_error_len = . - editor_open_error
editor_loaded:
    .ascii "loaded bytes="
editor_loaded_len = . - editor_loaded
editor_prompt:
    .ascii "ed> "
editor_prompt_len = . - editor_prompt
editor_backspace:
    .ascii "\b \b"
editor_backspace_len = . - editor_backspace
editor_newline:
    .ascii "\n"
editor_newline_len = . - editor_newline
editor_quit_cmd:
    .asciz ".q"
editor_print_cmd:
    .asciz ".p"
editor_write_cmd:
    .asciz ".w"
editor_saved:
    .ascii "saved\n"
editor_saved_len = . - editor_saved
editor_error:
    .ascii "editor: error\n"
editor_error_len = . - editor_error
editor_path:
    .asciz "/tmp/note"
editor_image_end:
