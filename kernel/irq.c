#include "kernel/irq.h"

#include <stdint.h>

#include "board.h"
#include "kernel/process.h"
#include "uart/pl011.h"

#define IRQ_HANDLER_SLOTS 64U

/*
 * EL1 IRQ dispatch table.
 *
 * Board code owns interrupt-controller acknowledge/end details. This layer only
 * maps interrupt IDs to small callbacks, saves the interrupted EL0 context when
 * a trap frame is present, and then gives the scheduler one preemption point.
 */

typedef struct {
    irq_handler_fn_t handler;
    void *context;
} irq_handler_entry_t;

static irq_handler_entry_t g_irq_handlers[IRQ_HANDLER_SLOTS];

int irq_register_handler(uint32_t irq, irq_handler_fn_t handler, void *context) {
    if (irq >= IRQ_HANDLER_SLOTS || handler == 0) {
        return -1;
    }

    g_irq_handlers[irq].handler = handler;
    g_irq_handlers[irq].context = context;

    return 0;
}

void irq_unregister_handler(uint32_t irq) {
    if (irq >= IRQ_HANDLER_SLOTS) {
        return;
    }

    g_irq_handlers[irq].handler = 0;
    g_irq_handlers[irq].context = 0;
}

void irq_handler_frame(exception_frame_t *frame) {
    process_t *current = process_current();
    uint32_t irq = board_irq_ack();

    if (current != 0 && frame != 0) {
        process_save_context(current, frame->x, frame->elr, frame->spsr,
                             frame->sp_el0);
    }

    if (board_irq_is_spurious(irq)) {
        return;
    }

    if (irq < IRQ_HANDLER_SLOTS && g_irq_handlers[irq].handler != 0) {
        g_irq_handlers[irq].handler(g_irq_handlers[irq].context);
    } else {
        uart_puts("IRQ unknown\n");
    }

    board_irq_end(irq);

    if (current != 0 && frame != 0) {
        current->pc = frame->elr;
        current->pstate = frame->spsr;
        current->sp = frame->sp_el0;

        (void)process_dispatch_next(current, frame, PROCESS_DISPATCH_PREEMPT);
    }
}

void irq_handler(void) {
    irq_handler_frame(0);
}
