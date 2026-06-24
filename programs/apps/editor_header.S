// programs/apps/editor_header.S
//
// Image header for the editor app. The editor logic now lives in
// programs/apps/editor.c and enters through libkarm's _start, so the
// only thing the header needs is the magic, the size, and the
// entry_offsets[0] pointer at libkarm's _start.
//
// editor_image_end lives in editor_end.S so it sits at the very tail
// of the flat image and editor_image_header's image_size field covers
// every byte of code the loader has to copy.

.section .user.image.header, "a"
.global editor_image_header
.type editor_image_header, %function
editor_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad editor_image_end - editor_image_header
    .quad _start - editor_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
