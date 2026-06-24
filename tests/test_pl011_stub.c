/*
 * Host-side stub for pl011.c.
 *
 * The real pl011.c uses aarch64 `yield` instructions in its spin
 * loops which the host gcc does not understand. The test binary
 * only needs uart_puts and uart_putc to silence the linker when
 * usb_core.c prints its debug messages; we route them to stdout.
 */

#include <stdint.h>
#include <stdio.h>

void uart_putc(char c) {
    fputc(c, stdout);
}

void uart_puts(const char *s) {
    while (*s != 0) {
        fputc(*s, stdout);
        s++;
    }
}