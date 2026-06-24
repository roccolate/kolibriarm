// programs/apps/clock_header.S
//
// Image header for the clock app. The clock logic now lives in
// programs/apps/clock.c and enters through libkarm's _start, so the
// only thing the header needs is the magic, the size, and the
// entry_offsets[0] pointer at libkarm's _start. Everything else is
// zero so the kernel's loader treats this as a flat image with a
// single entry.
//
// clock_image_end lives in clock_end.S so it sits at the very tail of
// the flat image and clock_image_header's image_size field covers
// every byte of code the loader has to copy.

.section .user.image.header, "a"
.global clock_image_header
.type clock_image_header, %function
clock_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad clock_image_end - clock_image_header
    .quad _start - clock_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
