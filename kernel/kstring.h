#ifndef ARMONIOS_KERNEL_KSTRING_H
#define ARMONIOS_KERNEL_KSTRING_H

/*
 * Shared kernel string utilities.
 *
 * kstreq() replaces the duplicate strcmp-style helpers that were copy-pasted
 * across bootfs, boot_program, and vfs.
 */

/**
 * Compare two NUL-terminated strings for equality.
 * Returns 1 if equal, 0 if different or if either pointer is NULL.
 */
int kstreq(const char *a, const char *b);

#endif /* ARMONIOS_KERNEL_KSTRING_H */
