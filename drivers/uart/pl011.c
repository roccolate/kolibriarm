#include "uart/pl011.h"

#include <stdint.h>

#include "input/input.h"
#include "kernel/console.h"

#define UART_DR        0x00
#define UART_FR        0x18
#define UART_IMSC      0x38
#define UART_MIS       0x40
#define UART_ICR       0x44
#define UART_LCRH      0x2c
#define UART_FR_RXFE   (1U << 4)
#define UART_FR_TXFF   (1U << 5)
#define UART_INT_RX    (1U << 4)
#define UART_INT_RT    (1U << 6)
#define UART_LCRH_FEN  (1U << 4)
static uint64_t g_uart_base;

static volatile uint32_t *uart_reg(uint32_t offset) {
    return (volatile uint32_t *)(g_uart_base + offset);
}

void uart_init(uint64_t base) {
    /* QEMU virt firmware leaves PL011 usable for polling output. */
    g_uart_base = base;
    *uart_reg(UART_ICR) = UART_INT_RX | UART_INT_RT;
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

void uart_enable_rx_irq(void) {
    *uart_reg(UART_IMSC) |= UART_INT_RX | UART_INT_RT;
}

void uart_irq_handler(void *context) {
    (void)context;

    uint32_t pending = *uart_reg(UART_MIS);
    if ((pending & (UART_INT_RX | UART_INT_RT)) == 0) {
        return;
    }

    while ((*uart_reg(UART_FR) & UART_FR_RXFE) == 0) {
        char c = (char)(*uart_reg(UART_DR) & 0xffU);
        input_event_t event = {
            .type = INPUT_EVENT_KEY_PRESS,
            .timestamp = 0,
            .data.key.key = (uint32_t)(uint8_t)c,
        };
        (void)input_queue_push(&event);
    }

    *uart_reg(UART_ICR) = UART_INT_RX | UART_INT_RT;
}

void uart_pump_input(void) {
    if (g_uart_base == 0) {
        return;
    }

    while ((*uart_reg(UART_FR) & UART_FR_RXFE) == 0) {
        char c = (char)(*uart_reg(UART_DR) & 0xffU);
        input_event_t event = {
            .type = INPUT_EVENT_KEY_PRESS,
            .timestamp = 0,
            .data.key.key = (uint32_t)(uint8_t)c,
        };
        (void)input_queue_push(&event);
    }
}


