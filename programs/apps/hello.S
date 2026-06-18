// KolibriARM app: hello
//
// Prints "Hello from EL0\n" and exits. The smallest possible app, used to
// verify the per-app build chain and the spawn-by-name path.

.section .user.image.header, "a"
.global hello_image_header
.type hello_image_header, %function
hello_image_header:
    .word 0x31494c4b              // USER_IMAGE_MAGIC
    .hword 80                     // header_size
    .hword 1                      // entry_count
    .quad hello_image_end - hello_image_header
    .quad hello_start - hello_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global hello_start
.type hello_start, %function

hello_start:
    mov x8, #3                    // sys_getpid
    svc #0
    cmp x0, #1
    b.eq 1f
    mov x0, #1
    mov x8, #1                    // sys_exit
    svc #0

1:
    mov x0, #1                    // fd = stdout
    adr x1, hello_msg
    mov x2, hello_msg_len
    mov x8, #43                   // sys_write
    svc #0

    mov x0, #0
    mov x8, #1                    // sys_exit
    svc #0

2:
    wfe
    b 2b

.section .user.image.rodata, "a"
hello_msg:
    .ascii "Hello from EL0\n"
hello_msg_len = . - hello_msg
hello_image_end:
