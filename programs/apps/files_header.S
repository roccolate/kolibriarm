// programs/apps/files_header.S
//
// Image header for the files app. The KLI1 header layout is defined in
// kernel/user_image_format.h; _start is provided by libkarm/crt0.S.

.section .user.image.header, "a"
.global files_image_header
.type files_image_header, %function
files_image_header:
    .word 0x31494c4b
    .hword 80
    .hword 1
    .quad files_image_end - files_image_header
    .quad _start - files_image_header
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .quad 0
