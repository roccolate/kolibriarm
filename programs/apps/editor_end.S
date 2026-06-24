// programs/apps/editor_end.S
//
// End-of-image marker for the editor app. editor_image_end has to
// live at the very tail of the flat image so that
// editor_image_header's image_size field correctly reports how many
// bytes the loader should copy into per-process memory.

.section .user.image.text, "ax"

.global editor_image_end
editor_image_end:
