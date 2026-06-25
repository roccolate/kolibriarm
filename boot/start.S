.section .text.boot
.global _start
.type _start, %function

_start:
    // QEMU and the Pi firmware pass the DTB pointer in x0. Keep it
    // across early setup so kernel_main receives the original value.
    mov x19, x0

    // Install the early kernel stack before calling any C code.
    adrp x0, __stack_top
    add x0, x0, :lo12:__stack_top
    mov sp, x0

    // Set VBAR_EL1 while the boot path is still single-threaded.
    bl exception_init

    // Clear zero-initialized storage before kernel_main touches globals.
    adrp x0, __bss_start
    add x0, x0, :lo12:__bss_start
    adrp x1, __bss_end
    add x1, x1, :lo12:__bss_end
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
