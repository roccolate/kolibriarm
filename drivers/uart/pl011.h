#ifndef KOLIBRIARM_UART_PL011_H
#define KOLIBRIARM_UART_PL011_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

#endif
