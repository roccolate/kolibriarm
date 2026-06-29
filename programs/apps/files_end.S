// programs/apps/files_end.S
//
// End-of-image marker for the files app. It must remain at the tail
// of the flat image so image_size covers code and rodata.

.section .user.image.end, "ax"

.global files_image_end
files_image_end:
