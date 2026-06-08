#include "uart/pl011.h"

#include <stdint.h>

#define PL011_UART_BASE 0x09000000UL

#define UART_DR        0x00
#define UART_FR        0x18
#define UART_FR_TXFF   (1U << 5)

static volatile uint32_t *uart_reg(uint32_t offset) {
    return (volatile uint32_t *)(PL011_UART_BASE + offset);
}

void uart_init(void) {
    /* QEMU virt firmware leaves PL011 usable for polling output. */
}

void uart_putc(char c) {
    if (c == '\n') {
        uart_putc('\r');
    }

    while ((*uart_reg(UART_FR) & UART_FR_TXFF) != 0) {
        __asm__ volatile("yield");
    }

    *uart_reg(UART_DR) = (uint32_t)c;
}

void uart_puts(const char *s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}
