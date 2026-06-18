// Common EL0 helpers shared by every KolibriARM app.
//
// Each app includes its own .user.image.header section with the
// USER_IMAGE_MAGIC and a single entry_offsets[0] pointing at the app entry
// label. This file provides the syscall wrappers and small text helpers so
// every app is built from its own entry plus this common blob.
//
// Syscall ABI (AArch64 SVC):
//   x0..x5 = args
//   x8     = syscall number
//   x0     = return value (negative on error)
// Caller-saved across SVC: x0..x7, x16, x17. x19..x28 are preserved.
//
// IMPORTANT: these helpers live in the .user.image.text section so the
// linker places them inside the app's flat image. The kernel copies the
// whole image (header.image_size bytes) into per-process memory, so anything
// outside the .user.image sections would be silently dropped at load time
// and the app would jump to unmapped memory.

.section .user.image.text, "ax"

.global common_write_cstr
.type common_write_cstr, %function

.global common_write
.type common_write, %function

.global common_streq
.type common_streq, %function

.global common_print_dec
.type common_print_dec, %function

// void common_write(int fd, const void *buf, uint64_t len)
common_write:
    mov x8, #43
    svc #0
    ret

// void common_write_cstr(int fd, const char *s)
common_write_cstr:
    sub sp, sp, #16
    str x19, [sp, #8]
    mov x19, x1
1:
    ldrb w4, [x19]
    cbz w4, 2f
    mov x1, x19
    mov x2, #1
    mov x8, #43
    svc #0
    add x19, x19, #1
    b 1b
2:
    ldr x19, [sp, #8]
    add sp, sp, #16
    ret

// int common_streq(const char *a, const char *b) -> 1 on equal, 0 otherwise
common_streq:
    ldrb w2, [x0], #1
    ldrb w3, [x1], #1
    cmp w2, w3
    b.ne 1f
    cbnz w2, common_streq
    mov x0, #1
    ret
1:
    mov x0, #0
    ret

// void common_print_dec(int fd, uint64_t value)
common_print_dec:
    sub sp, sp, #48
    str x19, [sp, #40]
    mov x3, sp
    mov x4, #0
    mov x19, x0
    cbnz x1, 3f

    mov w5, #'0'
    strb w5, [x3]
    mov x0, x19
    mov x1, x3
    mov x2, #1
    mov x8, #43
    svc #0
    ldr x19, [sp, #40]
    add sp, sp, #48
    ret

3:
    mov x6, #10
4:
    udiv x7, x1, x6
    msub x5, x7, x6, x1
    add w5, w5, #'0'
    strb w5, [x3, x4]
    add x4, x4, #1
    mov x1, x7
    cbnz x1, 4b

5:
    sub x4, x4, #1
    mov x0, x19
    add x1, x3, x4
    mov x2, #1
    mov x8, #43
    svc #0
    cbnz x4, 5b
    ldr x19, [sp, #40]
    add sp, sp, #48
    ret
