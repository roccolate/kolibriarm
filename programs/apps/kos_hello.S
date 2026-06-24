// KolibriARM app: kos_hello
//
// Smallest possible KOS-format app. Uses the KolibriOS header shape
// instead of our native KLI1 header so we can validate the loader's
// alternative-magic path on real hardware. The KOS header is a fixed
// 24 bytes: magic, image_size, mem_size, stack_size, entry_offset,
// reserved. The kernel user_image_load_flat recognises the magic
// 0x00534F4B ('KOS\0') and loads the image through the same copy
// helper as the KLI1 path. See Phase 10.5 of ROADMAP.md.

.equ SYS_EXIT,         1
.equ SYS_WRITE,       43
.equ STDOUT,           1

.equ KOS_HDR_SIZE,    24

.section .user.image.header, "a"
.global kos_hello_image_header
.type kos_hello_image_header, %function
kos_hello_image_header:
    .word 0x00534f4b                        // USER_KOS_MAGIC = 'KOS\0'
    .word kos_hello_image_end - kos_hello_image_header
    .word 0                                 // mem_size: 0 means image_size
    .word 0                                 // stack_size: 0 means default
    .word kos_hello_start - kos_hello_image_header
    .word 0                                 // reserved: must be 0

.section .user.image.text, "ax"
.global kos_hello_start
.type kos_hello_start, %function

kos_hello_start:
    mov x0, #STDOUT
    adr x1, kos_hello_msg
    mov x2, kos_hello_msg_len
    mov x8, #SYS_WRITE
    svc #0

    mov x0, #0
    mov x8, #SYS_EXIT
    svc #0

1:
    wfe
    b 1b

.section .user.image.rodata, "a"
kos_hello_msg:
    .ascii "Hello from KOS image\n"
kos_hello_msg_len = . - kos_hello_msg
kos_hello_image_end: