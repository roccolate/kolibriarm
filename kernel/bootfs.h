#ifndef KOLIBRIARM_KERNEL_BOOTFS_H
#define KOLIBRIARM_KERNEL_BOOTFS_H

#include <stdint.h>

/*
 * Read-only boot filesystem file descriptor.
 *
 * `data` points directly at the embedded KLI1 app blob in the kernel image.
 * bootfs_find returns a stable pointer per registered app, so later lookups do
 * not invalidate an earlier descriptor.
 */
typedef struct {
    const char *name;
    const uint8_t *data;
    uint64_t size;
} bootfs_file_t;

const bootfs_file_t *bootfs_find(const char *name);
int bootfs_read(const char *name, uint64_t offset, uint8_t *buffer,
                uint64_t capacity, uint64_t *bytes_read);
int bootfs_mount_vfs(void);

#endif
