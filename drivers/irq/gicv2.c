#include "irq/gicv2.h"

#include <stdint.h>

#define GICD_BASE 0x08000000UL
#define GICC_BASE 0x08010000UL

#define GICD_CTLR       0x000
#define GICD_ISENABLER  0x100
#define GICD_IPRIORITYR 0x400

#define GICC_CTLR 0x000
#define GICC_PMR  0x004
#define GICC_IAR  0x00c
#define GICC_EOIR 0x010

static volatile uint32_t *gicd_reg(uint32_t offset) {
    return (volatile uint32_t *)(GICD_BASE + offset);
}

static volatile uint32_t *gicc_reg(uint32_t offset) {
    return (volatile uint32_t *)(GICC_BASE + offset);
}

static volatile uint8_t *gicd_reg8(uint32_t offset) {
    return (volatile uint8_t *)(GICD_BASE + offset);
}

void gicv2_init(void) {
    *gicd_reg(GICD_CTLR) = 0;
    *gicc_reg(GICC_CTLR) = 0;

    *gicc_reg(GICC_PMR) = 0xff;

    *gicd_reg(GICD_CTLR) = 1;
    *gicc_reg(GICC_CTLR) = 1;
}

void gicv2_enable_irq(uint32_t irq) {
    uint32_t reg = GICD_ISENABLER + (irq / 32U) * sizeof(uint32_t);
    uint32_t bit = irq % 32U;

    *gicd_reg8(GICD_IPRIORITYR + irq) = 0x80;
    *gicd_reg(reg) = 1U << bit;
}

uint32_t gicv2_ack_irq(void) {
    return *gicc_reg(GICC_IAR) & 0x3ffU;
}

void gicv2_end_irq(uint32_t irq) {
    *gicc_reg(GICC_EOIR) = irq;
}
