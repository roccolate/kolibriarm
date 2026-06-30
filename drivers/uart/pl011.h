#ifndef ARMONIOS_UART_PL011_H
#define ARMONIOS_UART_PL011_H

#include <stdint.h>

void uart_init(uint64_t base);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_pump_input(void);
void uart_enable_rx_irq(void);
void uart_irq_handler(void *context);

#endif
