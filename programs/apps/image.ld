SECTIONS {
    . = 0;

    .user_image : ALIGN(16) {
        KEEP(*(.user.image.header*))
        *(.user.image.text*)
        *(.user.image.rodata*)
    }
}
