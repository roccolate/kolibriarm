#ifndef KOLIBRIARM_KERNEL_EXCEPTIONS_H
#define KOLIBRIARM_KERNEL_EXCEPTIONS_H

#include <stdint.h>

/**
 * exception_init - Install the EL1 exception vector table into VBAR_EL1.
 */
void exception_init(void);

/**
 * exception_vector_base - Return the current VBAR_EL1 value.
 */
uint64_t exception_vector_base(void);

/**
 * exception_handler - Print exception diagnostics and halt.
 */
void exception_handler(uint64_t esr, uint64_t far, uint64_t elr, uint64_t kind);

#endif
