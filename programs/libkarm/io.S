// libkarm/io.S
//
// Small syscall-backed I/O conveniences shared by freestanding apps.
// Keep this file tiny: it is linked into every shipped app image.

.section .user.image.text, "ax"

.global kli_write_cstr
.type kli_write_cstr, %function

.equ KLI_SYS_WRITE, 43

// void kli_write_cstr(int fd, const char *s)
//
// Writes one byte at a time through SYS_WRITE (43). That keeps the
// helper usable before userland has buffering or strlen-linked apps.
kli_write_cstr:
    cbz x1, 2f
    stp x19, x20, [sp, #-16]!
    mov x19, x1
    sxtw x20, w0
1:
    ldrb w2, [x19]
    cbz w2, 3f
    mov x0, x20
    mov x1, x19
    mov x2, #1
    mov x8, #KLI_SYS_WRITE
    svc #0
    add x19, x19, #1
    b 1b
3:
    ldp x19, x20, [sp], #16
2:
    ret
