// KolibriARM app: fault
//
// Writes to an unmapped address so the kernel can verify that lower-EL data
// aborts convert into a clean process exit and the scheduler keeps going.

.section .user.image.header, "a"
.global fault_image_header
.type fault_image_header, %function
fault_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad fault_image_end - fault_image_header
    .quad fault_start - fault_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0

.section .user.image.text, "ax"
.global fault_start
.type fault_start, %function

fault_start:
    mov x0, #1
    adr x1, fault_msg
    mov x2, fault_msg_len
    mov x8, #43
    svc #0

    movz x0, #0x0000
    movk x0, #0x00f0, lsl #16
    str xzr, [x0]                 // data abort on unmapped VA

    mov x0, #0x55
    mov x8, #1                    // sys_exit (unreachable)
    svc #0

1:
    wfe
    b 1b

.section .user.image.rodata, "a"
fault_msg:
    .ascii "fault: about to crash\n"
fault_msg_len = . - fault_msg
fault_image_end:
