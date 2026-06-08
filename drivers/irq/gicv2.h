#ifndef KOLIBRIARM_DRIVERS_IRQ_GICV2_H
#define KOLIBRIARM_DRIVERS_IRQ_GICV2_H

#include <stdint.h>

#define GIC_SPURIOUS_IRQ 1023U

void gicv2_init(void);
void gicv2_enable_irq(uint32_t irq);
uint32_t gicv2_ack_irq(void);
void gicv2_end_irq(uint32_t irq);

#endif
