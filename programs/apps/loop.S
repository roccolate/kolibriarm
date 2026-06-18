// KolibriARM app: loop
//
// Yields forever. Used to test that sys_yield cooperates with the scheduler
// and that the panel can list running apps.

.section .user.image.header, "a"
.global loop_image_header
.type loop_image_header, %function
loop_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad loop_image_end - loop_image_header
    .quad loop_start - loop_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global loop_start
.type loop_start, %function

loop_start:
    mov x0, #1
    adr x1, loop_msg
    mov x2, loop_msg_len
    mov x8, #43
    svc #0

1:
    mov x8, #2                    // sys_yield
    svc #0
    b 1b

.section .user.image.rodata, "a"
loop_msg:
    .ascii "loop: running\n"
loop_msg_len = . - loop_msg
loop_image_end:
