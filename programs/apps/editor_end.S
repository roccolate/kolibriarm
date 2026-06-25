// programs/apps/editor_end.S
//
// End-of-image marker for the editor app. editor_image_end has to
// live at the very tail of the flat image so that
// editor_image_header's image_size field correctly reports how many
// bytes the loader should copy into per-process memory.
//
// The marker is placed in its own .user.image.end section so the
// linker script can position it after every other .user_image
// rodata/text fragment — otherwise the strings that follow the
// code in editor.c's .rodata would land past editor_image_end and
// never be copied into per-process memory.

.section .user.image.end, "ax"

.global editor_image_end
editor_image_end:
