.section .text.boot
.global _start
.type _start, %function

_start:
    mov x19, x0

    ldr x0, =__stack_top
    mov sp, x0

    bl exception_init

    ldr x0, =__bss_start
    ldr x1, =__bss_end
    bl zero_bss

    mov x0, x19
    bl kernel_main

.Lhalt:
    wfe
    b .Lhalt

.type zero_bss, %function
zero_bss:
    cmp x0, x1
    b.hs .Lzero_done

.Lzero_loop:
    str xzr, [x0], #8
    cmp x0, x1
    b.lo .Lzero_loop

.Lzero_done:
    ret
