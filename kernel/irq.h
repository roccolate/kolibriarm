#ifndef KOLIBRIARM_KERNEL_IRQ_H
#define KOLIBRIARM_KERNEL_IRQ_H

#include <stdint.h>

#include "kernel/exceptions.h"

/*
 * Minimal IRQ callback registry.
 *
 * Handlers run in EL1 interrupt context, must not block, and receive only their
 * registration context. irq_handler_frame is entered from the exception vector
 * when an interrupted EL0 frame is available; irq_handler covers early/kernel
 * IRQ paths that have no user frame to reschedule from.
 */

typedef void (*irq_handler_fn_t)(void *context);

int irq_register_handler(uint32_t irq, irq_handler_fn_t handler, void *context);
void irq_unregister_handler(uint32_t irq);
void irq_handler(void);
void irq_handler_frame(exception_frame_t *frame);
void irq_enable(void);
void irq_disable(void);

#endif
