#include "kernel/print.h"

#include "uart/pl011.h"

/*
 * Boot/kernel numeric printing.
 *
 * These helpers write directly to UART and intentionally do not own console
 * line discipline, command parsing, buffering, or printf-style formatting.
 */

void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> shift) & 0xf]);
    }
}

void print_hex8(uint8_t value) {
    static const char digits[] = "0123456789abcdef";

    uart_putc(digits[(value >> 4) & 0xf]);
    uart_putc(digits[value & 0xf]);
}

void print_dec64(uint64_t value) {
    char buf[20];
    uint32_t i = 0;

    if (value == 0) {
        uart_putc('0');
        return;
    }

    while (value > 0 && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

void print_signed32(int32_t value) {
    if (value < 0) {
        uart_putc('-');
        /* Use uint32_t to handle INT32_MIN without overflow. */
        print_dec64((uint64_t)(uint32_t)(-(int64_t)value));
    } else {
        print_dec64((uint64_t)(uint32_t)value);
    }
}
