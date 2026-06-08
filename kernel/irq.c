#include "kernel/irq.h"

#include <stdint.h>

#include "irq/gicv2.h"
#include "kernel/timer/timer.h"
#include "uart/pl011.h"

void irq_handler(void) {
    uint32_t irq = gicv2_ack_irq();

    if (irq == GIC_SPURIOUS_IRQ) {
        return;
    }

    if (irq == TIMER_IRQ) {
        timer_handle_irq();
    } else {
        uart_puts("IRQ unknown\n");
    }

    gicv2_end_irq(irq);
}
