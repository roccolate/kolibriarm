// programs/apps/monitor_end.S
//
// End-of-image marker for the monitor app. monitor_image_end has to
// live at the very tail of the flat image so that
// monitor_image_header's image_size field correctly reports how many
// bytes the loader should copy into per-process memory.

.section .user.image.end, "ax"

.global monitor_image_end
monitor_image_end:
