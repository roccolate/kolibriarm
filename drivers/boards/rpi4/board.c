#include "boards/rpi4/board.h"

#include "irq/gicv2.h"
#include "kernel/mm/vmm.h"
#include "storage/emmc.h"
#include "uart/pl011.h"

static emmc_device_t g_emmc_dev;
static int g_emmc_initialized = 0;

static volatile uint32_t *mailbox_reg(uint32_t offset) {
    return (volatile uint32_t *)(RPI4_MAILBOX_BASE + offset);
}

__attribute__((unused)) static int mailbox_call(uint32_t channel, uint32_t *data) {
    uint32_t r;

    *mailbox_reg(0x20) = 0;
    *mailbox_reg(0x10) = ((uint32_t)((uintptr_t)data) & ~0xF) | (channel & 0xF);

    for (uint32_t i = 0; i < 1000000; i++) {
        r = *mailbox_reg(0x18);
        if (r & 0x40000000) {
            return (r & 0xF) == channel ? 0 : -1;
        }
    }

    return -1;
}

const char *board_name(void) {
    return "rpi4-bcm2711";
}

void board_early_init(void) {
    uart_init(RPI4_UART0_BASE);
}

void board_irq_init(void) {
    gicv2_init(RPI4_GIC_DIST_BASE, RPI4_GIC_CPU_BASE);
}

void board_irq_enable(uint32_t irq) {
    gicv2_enable_irq(irq);
}

uint32_t board_irq_ack(void) {
    return gicv2_ack_irq();
}

void board_irq_end(uint32_t irq) {
    gicv2_end_irq(irq);
}

int board_irq_is_spurious(uint32_t irq) {
    return irq == GIC_SPURIOUS_IRQ;
}

uint32_t board_uart0_irq(void) {
    return RPI4_UART0_IRQ;
}

uint64_t board_virtio_mmio_base(void) {
    return RPI4_VIRTIO_MMIO_BASE;
}

uint64_t board_virtio_mmio_size(void) {
    return RPI4_VIRTIO_MMIO_SIZE;
}

uint64_t board_virtio_mmio_stride(void) {
    return RPI4_VIRTIO_MMIO_STRIDE;
}

int board_map_mmio(uint64_t *pgd) {
    int status;

    status = vmm_map_page(pgd, RPI4_UART0_BASE, RPI4_UART0_BASE,
                          VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
    if (status != 0) {
        return status;
    }

    status = vmm_map_range(pgd, RPI4_GIC_DIST_BASE,
                           RPI4_GIC_DIST_BASE,
                           RPI4_GIC_MMIO_SIZE,
                           VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
    if (status != 0) {
        return status;
    }

    status = vmm_map_range(pgd, RPI4_VIRTIO_MMIO_BASE,
                           RPI4_VIRTIO_MMIO_BASE,
                           RPI4_VIRTIO_MMIO_SIZE,
                           VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
    if (status != 0) {
        return status;
    }

    status = vmm_map_range(pgd, RPI4_MAILBOX_BASE,
                           RPI4_MAILBOX_BASE,
                           RPI4_MAILBOX_SIZE,
                           VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
    if (status != 0) {
        return status;
    }

    status = vmm_map_range(pgd, RPI4_EMMC_BASE,
                           RPI4_EMMC_BASE,
                           0x1000,
                           VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
    if (status != 0) {
        return status;
    }

    return 0;
}

int board_emmc_read(uint32_t lba, uint32_t count, void *buffer) {
    if (!g_emmc_initialized) {
        if (emmc_init(&g_emmc_dev, RPI4_EMMC_BASE) != 0) {
            return -1;
        }
        g_emmc_initialized = 1;
    }

    return emmc_read_sector(&g_emmc_dev, lba, count, buffer);
}

int board_emmc_write(uint32_t lba, uint32_t count, const void *buffer) {
    if (!g_emmc_initialized) {
        if (emmc_init(&g_emmc_dev, RPI4_EMMC_BASE) != 0) {
            return -1;
        }
        g_emmc_initialized = 1;
    }

    return emmc_write_sector(&g_emmc_dev, lba, count, buffer);
}

int board_storage_read(uint32_t lba, uint32_t count, void *buffer) {
    return board_emmc_read(lba, count, buffer);
}

int board_storage_write(uint32_t lba, uint32_t count, const void *buffer) {
    return board_emmc_write(lba, count, buffer);
}

int board_storage_init(void) {
    return emmc_init(&g_emmc_dev, RPI4_EMMC_BASE);
}