#ifndef ARMONIOS_KERNEL_PRINT_H
#define ARMONIOS_KERNEL_PRINT_H

#include <stdint.h>

/*
 * Minimal UART-backed numeric output.
 *
 * Keep panic/boot formatting here and keep interactive command handling in
 * console.c so the two paths do not grow duplicate line-discipline code.
 */

void print_hex64(uint64_t value);
void print_hex8(uint8_t value);
void print_dec64(uint64_t value);
void print_signed32(int32_t value);

#endif
