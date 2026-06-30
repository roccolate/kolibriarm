#ifndef ARMONIOS_KERNEL_KSTRING_H
#define ARMONIOS_KERNEL_KSTRING_H

#include <stdint.h>

/*
 * Shared kernel string and memory utilities.
 *
 * kstreq() replaces the duplicate strcmp-style helpers that were copy-pasted
 * across bootfs, boot_program, and vfs.
 *
 * kmemcpy/kmemset/kmemcmp/kmemzero replace the duplicate byte-loop helpers
 * that were copy-pasted across dhcp.c, xhci.c, and virtio_net.c.
 */

/**
 * Compare two NUL-terminated strings for equality.
 * Returns 1 if equal, 0 if different or if either pointer is NULL.
 */
int kstreq(const char *a, const char *b);

/** Copy @len bytes from @src to @dst. Returns @dst. */
void *kmemcpy(void *dst, const void *src, uint32_t len);

/** Fill @len bytes of @dst with @val. Returns @dst. */
void *kmemset(void *dst, uint8_t val, uint32_t len);

/** Compare @len bytes. Returns 0 if equal, or the difference of the first
 *  mismatched byte (standard memcmp semantics). */
int kmemcmp(const void *a, const void *b, uint32_t len);

/** Zero @len bytes starting at @ptr. */
void kmemzero(void *ptr, uint32_t len);

#endif /* ARMONIOS_KERNEL_KSTRING_H */
