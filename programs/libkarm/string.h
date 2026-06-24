// libkarm/string.h
//
// Minimal freestanding string and memory helpers. Only what the
// existing apps actually need today: byte-block copy/fill, the usual
// C string trio (length, compare, bounded copy), and a small integer
// formatter for the monitor / clock to render counters without
// rolling their own. No printf-style formatting, no locales, no
// heap.

#ifndef KOLIBRIARM_PROGRAMS_LIBKARM_STRING_H
#define KOLIBRIARM_PROGRAMS_LIBKARM_STRING_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);

size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);

// strlcpy copies up to dst_size - 1 bytes from src into dst,
// null-terminates, and returns the length of src (excluding the
// terminator) — same shape as the BSD helper.
size_t strlcpy(char *dst, const char *src, size_t dst_size);

// kli_utoa / kli_itoa write the decimal representation of `value`
// into `buf`, null-terminate, and return the number of bytes written
// excluding the terminator. `buf` must hold at least 22 bytes
// (sign + 19 uint64 digits + NUL); smaller buffers truncate.
size_t kli_utoa(uint64_t value, char *buf, size_t buf_size);
size_t kli_itoa(int64_t  value, char *buf, size_t buf_size);

#endif
