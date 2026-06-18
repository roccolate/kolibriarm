// KolibriARM app: shell
//
// A minimal command line. The first EL0 program the user meets. It reads
// characters from stdin (sys_read on fd 0), echoes them to stdout, and
// dispatches a fixed set of one-word commands. Full argument parsing lands
// with the panel/desktop work in Phase 10.4.

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
    mov x8, #3                    // sys_getpid
    svc #0
    cmp x0, #1
    b.eq 1f
    mov x0, #1
    mov x8, #1                    // sys_exit
    svc #0

1:
    sub sp, sp, #96
    mov x19, sp                   // line buffer (64 bytes)
    mov x26, #0                   // last spawn pid (for "kill last")

    mov x0, #1
    adr x1, shell_banner
    mov x2, shell_banner_len
    bl common_write

2:
    mov x20, #0                   // line length
    mov x0, #1
    adr x1, shell_prompt
    mov x2, shell_prompt_len
    bl common_write

3:
    mov x0, #0
    add x1, sp, #80
    mov x2, #1
    mov x8, #42                   // sys_read
    svc #0
    cmp x0, #1
    b.eq 4f
    mov x8, #2
    svc #0
    b 3b

4:
    ldrb w21, [sp, #80]
    cmp w21, #13
    b.eq 6f
    cmp w21, #10
    b.eq 6f
    cmp w21, #8
    b.eq 5f
    cmp w21, #127
    b.eq 5f
    cmp w21, #32
    b.lo 3b
    cmp w21, #126
    b.hi 3b
    cmp x20, #63
    b.hs 3b
    strb w21, [x19, x20]
    add x20, x20, #1
    mov x0, #1
    add x1, sp, #80
    mov x2, #1
    bl common_write
    b 3b

5:
    cbz x20, 3b
    sub x20, x20, #1
    mov x0, #1
    adr x1, shell_backspace
    mov x2, shell_backspace_len
    bl common_write
    b 3b

6:
    mov w22, #0
    strb w22, [x19, x20]
    mov x0, #1
    adr x1, shell_newline
    mov x2, #1
    bl common_write
    cbz x20, 2b

    mov x0, x19
    adr x1, shell_help_cmd
    bl common_streq
    cbnz x0, 10f

    mov x0, x19
    adr x1, shell_ls_cmd
    bl common_streq
    cbnz x0, 20f

    mov x0, x19
    adr x1, shell_ls_fat_cmd
    bl common_streq
    cbnz x0, 21f

    mov x0, x19
    adr x1, shell_ls_root_cmd
    bl common_streq
    cbnz x0, 20f

    mov x0, x19
    adr x1, shell_mem_cmd
    bl common_streq
    cbnz x0, 30f

    mov x0, x19
    adr x1, shell_ps_cmd
    bl common_streq
    cbnz x0, 40f

    mov x0, x19
    adr x1, shell_ticks_cmd
    bl common_streq
    cbnz x0, 50f

    mov x0, x19
    adr x1, shell_run_hello_cmd
    bl common_streq
    cbnz x0, 60f

    mov x0, x19
    adr x1, shell_run_loop_cmd
    bl common_streq
    cbnz x0, 61f

    mov x0, x19
    adr x1, shell_run_monitor_cmd
    bl common_streq
    cbnz x0, 62f

    mov x0, x19
    adr x1, shell_run_editor_cmd
    bl common_streq
    cbnz x0, 63f

    mov x0, x19
    adr x1, shell_kill_last_cmd
    bl common_streq
    cbnz x0, 70f

    mov x0, x19
    adr x1, shell_pwd_cmd
    bl common_streq
    cbnz x0, 80f

    mov x0, x19
    adr x1, shell_cd_root_cmd
    bl common_streq
    cbnz x0, 81f

    mov x0, x19
    adr x1, shell_exit_cmd
    bl common_streq
    cbnz x0, 90f

    mov x0, #1
    adr x1, shell_unknown
    mov x2, shell_unknown_len
    bl common_write
    b 2b

// help
10:
    mov x0, #1
    adr x1, shell_help_msg
    mov x2, shell_help_msg_len
    bl common_write
    b 2b

// ls  or  ls /
20:
    adr x0, shell_root_path
    mov x1, x19
    mov x2, #64
    mov x8, #46                   // sys_readdir
    svc #0
    tbnz x0, #63, 22f
    mov x2, x0
    mov x0, #1
    mov x1, x19
    bl common_write
    b 2b
22:
    mov x0, #1
    adr x1, shell_ls_error
    mov x2, shell_ls_error_len
    bl common_write
    b 2b

// ls /fat
21:
    adr x0, shell_fat_path
    mov x1, x19
    mov x2, #64
    mov x8, #46
    svc #0
    tbnz x0, #63, 22b
    mov x2, x0
    mov x0, #1
    mov x1, x19
    bl common_write
    b 2b

// mem
30:
    mov x0, x19
    mov x8, #101                 // sys_meminfo
    svc #0
    tbnz x0, #63, 31f
    mov x0, #1
    adr x1, shell_mem_total
    mov x2, shell_mem_total_len
    bl common_write
    ldr x0, [x19]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, shell_mem_free
    mov x2, shell_mem_free_len
    bl common_write
    ldr x0, [x19, #8]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, shell_newline
    mov x2, #1
    bl common_write
    b 2b
31:
    mov x0, #1
    adr x1, shell_mem_error
    mov x2, shell_mem_error_len
    bl common_write
    b 2b

// ps
40:
    mov x0, x19
    mov x1, #4
    mov x8, #102                 // sys_proclist
    svc #0
    tbnz x0, #63, 41f
    mov x23, x0
    mov x24, x19
    mov x25, #0
    mov x0, #1
    adr x1, shell_ps_header
    mov x2, shell_ps_header_len
    bl common_write
42:
    cmp x25, x23
    b.hs 2b
    ldr w0, [x24]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, shell_space
    mov x2, #1
    bl common_write
    ldr w0, [x24, #4]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, shell_space
    mov x2, #1
    bl common_write
    mov x0, #1
    add x1, x24, #8
    bl common_write_cstr
    mov x0, #1
    adr x1, shell_newline
    mov x2, #1
    bl common_write
    add x24, x24, #24
    add x25, x25, #1
    b 42b
41:
    mov x0, #1
    adr x1, shell_ps_error
    mov x2, shell_ps_error_len
    bl common_write
    b 2b

// ticks
50:
    mov x0, x19
    mov x8, #100                 // sys_timeinfo
    svc #0
    tbnz x0, #63, 51f
    mov x0, #1
    adr x1, shell_ticks_timer
    mov x2, shell_ticks_timer_len
    bl common_write
    ldr x0, [x19]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, shell_ticks_sched
    mov x2, shell_ticks_sched_len
    bl common_write
    ldr x0, [x19, #8]
    mov x1, x0
    mov x0, #1
    bl common_print_dec
    mov x0, #1
    adr x1, shell_newline
    mov x2, #1
    bl common_write
    b 2b
51:
    mov x0, #1
    adr x1, shell_ticks_error
    mov x2, shell_ticks_error_len
    bl common_write
    b 2b

// run <name>
60:
    adr x0, shell_app_hello
    mov x8, #4                   // sys_spawn
    svc #0
    tbnz x0, #63, 65f
    mov x26, x0
    mov x0, #1
    adr x1, shell_spawned
    mov x2, shell_spawned_len
    bl common_write
    b 2b
61:
    adr x0, shell_app_loop
    mov x8, #4
    svc #0
    tbnz x0, #63, 65f
    mov x26, x0
    b 2b
62:
    adr x0, shell_app_monitor
    mov x8, #4
    svc #0
    tbnz x0, #63, 65f
    mov x26, x0
    b 2b
63:
    adr x0, shell_app_editor
    mov x8, #4
    svc #0
    tbnz x0, #63, 65f
    mov x26, x0
    b 2b
65:
    mov x0, #1
    adr x1, shell_run_error
    mov x2, shell_run_error_len
    bl common_write
    b 2b

// kill last
70:
    cbz x26, 71f
    mov x0, x26
    mov x8, #7                   // sys_kill
    svc #0
    mov x26, #0
    mov x0, #1
    adr x1, shell_killed
    mov x2, shell_killed_len
    bl common_write
    b 2b
71:
    mov x0, #1
    adr x1, shell_kill_error
    mov x2, shell_kill_error_len
    bl common_write
    b 2b

// pwd
80:
    mov x0, #1
    adr x1, shell_pwd_msg
    mov x2, shell_pwd_msg_len
    bl common_write
    b 2b

// cd /
81:
    mov x0, #1
    adr x1, shell_newline
    mov x2, #1
    bl common_write
    b 2b

// exit
90:
    add sp, sp, #96
    mov x0, #0
    mov x8, #1
    svc #0

.section .user.image.rodata, "a"
shell_banner:
    .ascii "user shell\n"
shell_banner_len = . - shell_banner
shell_prompt:
    .ascii "u> "
shell_prompt_len = . - shell_prompt
shell_backspace:
    .ascii "\b \b"
shell_backspace_len = . - shell_backspace
shell_newline:
    .ascii "\n"
shell_help_cmd:
    .asciz "help"
shell_ls_cmd:
    .asciz "ls"
shell_ls_root_cmd:
    .asciz "ls /"
shell_ls_fat_cmd:
    .asciz "ls /fat"
shell_mem_cmd:
    .asciz "mem"
shell_ps_cmd:
    .asciz "ps"
shell_ticks_cmd:
    .asciz "ticks"
shell_run_hello_cmd:
    .asciz "run hello"
shell_run_loop_cmd:
    .asciz "run loop"
shell_run_monitor_cmd:
    .asciz "run monitor"
shell_run_editor_cmd:
    .asciz "run editor"
shell_kill_last_cmd:
    .asciz "kill last"
shell_pwd_cmd:
    .asciz "pwd"
shell_cd_root_cmd:
    .asciz "cd /"
shell_exit_cmd:
    .asciz "exit"
shell_help_msg:
    .ascii "help, ls, ls /fat, ps, ticks, pwd, cd /, mem, run hello, run loop, run editor, run monitor, kill last, exit\n"
shell_help_msg_len = . - shell_help_msg
shell_space:
    .ascii " "
shell_unknown:
    .ascii "unknown command\n"
shell_unknown_len = . - shell_unknown
shell_ls_error:
    .ascii "ls failed\n"
shell_ls_error_len = . - shell_ls_error
shell_mem_total:
    .ascii "pages total="
shell_mem_total_len = . - shell_mem_total
shell_mem_free:
    .ascii " free="
shell_mem_free_len = . - shell_mem_free
shell_mem_error:
    .ascii "mem failed\n"
shell_mem_error_len = . - shell_mem_error
shell_ps_header:
    .ascii "pid state name\n"
shell_ps_header_len = . - shell_ps_header
shell_ps_error:
    .ascii "ps failed\n"
shell_ps_error_len = . - shell_ps_error
shell_ticks_timer:
    .ascii "timer="
shell_ticks_timer_len = . - shell_ticks_timer
shell_ticks_sched:
    .ascii " sched="
shell_ticks_sched_len = . - shell_ticks_sched
shell_ticks_error:
    .ascii "ticks failed\n"
shell_ticks_error_len = . - shell_ticks_error
shell_spawned:
    .ascii "spawned\n"
shell_spawned_len = . - shell_spawned
shell_killed:
    .ascii "killed\n"
shell_killed_len = . - shell_killed
shell_kill_error:
    .ascii "kill failed\n"
shell_kill_error_len = . - shell_kill_error
shell_run_error:
    .ascii "run failed\n"
shell_run_error_len = . - shell_run_error
shell_pwd_msg:
    .ascii "/\n"
shell_pwd_msg_len = . - shell_pwd_msg
shell_root_path:
    .asciz "/"
shell_fat_path:
    .asciz "/fat"
shell_app_hello:
    .asciz "/kolibri/hello"
shell_app_loop:
    .asciz "/kolibri/loop"
shell_app_monitor:
    .asciz "/kolibri/monitor"
shell_app_editor:
    .asciz "/kolibri/editor"
shell_image_end:
