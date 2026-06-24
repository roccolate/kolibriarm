// libkarm/string.c
//
// Minimal freestanding string and memory helpers. Only what the
// existing apps actually need today: byte-block copy/fill, the usual
// C string trio (length, compare, bounded copy), and a small integer
// formatter for the monitor / clock to render counters without
// rolling their own. No printf-style formatting, no locales, no
// heap.
//
// Each function is placed in `.user.image.text` explicitly because
// programs/apps/image.ld only collects `.user.image.{header,text,
// rodata}*` into the flat image that the kernel copies into
// per-process memory. Anything left in the default `.text` would be
// silently dropped at load time.

#include "string.h"

#define KLI_TEXT __attribute__((section(".user.image.text")))

KLI_TEXT
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

KLI_TEXT
void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    unsigned char v = (unsigned char)c;
    while (n--) {
        *d++ = v;
    }
    return dst;
}

KLI_TEXT
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

KLI_TEXT
size_t strlen(const char *s) {
    size_t n = 0;
    while (*s++) {
        n++;
    }
    return n;
}

KLI_TEXT
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

KLI_TEXT
size_t strlcpy(char *dst, const char *src, size_t dst_size) {
    size_t src_len = strlen(src);
    size_t copy_len = src_len;
    if (dst_size == 0) {
        return src_len;
    }
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1;
    }
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    return src_len;
}

KLI_TEXT
size_t kli_utoa(uint64_t value, char *buf, size_t buf_size) {
    char tmp[24];
    size_t i = 0;
    size_t out = 0;
    if (buf_size == 0) {
        return 0;
    }
    if (value == 0) {
        tmp[i++] = '0';
    } else {
        while (value > 0 && i < sizeof(tmp)) {
            tmp[i++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    while (i > 0 && out + 1 < buf_size) {
        buf[out++] = tmp[--i];
    }
    buf[out] = '\0';
    return out;
}

KLI_TEXT
size_t kli_itoa(int64_t value, char *buf, size_t buf_size) {
    int negative = (value < 0);
    uint64_t magnitude;
    char digits[24];
    size_t i = 0;
    size_t out = 0;
    if (buf_size == 0) {
        return 0;
    }
    if (negative) {
        // Avoid -(INT64_MIN) which overflows. Flip bits and add one
        // by hand to get the magnitude of any int64.
        magnitude = ~(uint64_t)value + 1u;
    } else {
        magnitude = (uint64_t)value;
    }
    if (negative && out + 1 < buf_size) {
        buf[out++] = '-';
    }
    if (magnitude == 0) {
        digits[i++] = '0';
    } else {
        while (magnitude > 0 && i < sizeof(digits)) {
            digits[i++] = (char)('0' + (magnitude % 10));
            magnitude /= 10;
        }
    }
    while (i > 0 && out + 1 < buf_size) {
        buf[out++] = digits[--i];
    }
    buf[out] = '\0';
    return out;
}
