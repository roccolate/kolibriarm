#ifndef KOLIBRIARM_UART_PL011_H
#define KOLIBRIARM_UART_PL011_H

#include <stdint.h>

void uart_init(uint64_t base);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_hex(uint64_t value);
void uart_put_hex_byte(uint8_t value);
int uart_getc_nonblock(void);
uint32_t uart_rx_available(void);
void uart_enable_rx_irq(void);
void uart_irq_handler(void *context);
void uart_pump_input(void);
int uart_getc_nonblock(void);
uint32_t uart_rx_available(void);

#endif
