#include "boards/qemu_virt/board.h"

#include "audio/virtio_snd.h"
#include "input/virtio_input.h"
#include "irq/gicv2.h"
#include "kernel/mm/vmm.h"
#include "storage/virtio_blk.h"
#include "uart/pl011.h"

static virtio_blk_device_t g_blk_dev;
static virtio_input_device_t g_input_dev;
static virtio_snd_device_t g_snd_dev;

int board_storage_read(uint32_t lba, uint32_t count, void *buffer) {
    (void)count;
    return virtio_blk_read_sector(&g_blk_dev, lba, buffer);
}

int board_storage_write(uint32_t lba, uint32_t count, const void *buffer) {
    (void)count;
    return virtio_blk_write_sector(&g_blk_dev, lba, buffer);
}

int board_storage_init(void) {
    virtio_blk_info_t info;
    uint64_t base;

    if (virtio_blk_probe_range(board_virtio_mmio_base(),
                               board_virtio_mmio_size(),
                               board_virtio_mmio_stride(), &base,
                               &info) != 0) {
        return -1;
    }

    return virtio_blk_init(&g_blk_dev, base);
}

const char *board_name(void) {
    return "qemu-virt";
}

void board_early_init(void) {
    uart_init(QEMU_VIRT_UART0_BASE);
}

void board_irq_init(void) {
    gicv2_init(QEMU_VIRT_GIC_DIST_BASE, QEMU_VIRT_GIC_CPU_BASE);
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
    return QEMU_VIRT_UART0_IRQ;
}

uint64_t board_virtio_mmio_base(void) {
    return QEMU_VIRT_VIRTIO_MMIO_BASE;
}

uint64_t board_virtio_mmio_size(void) {
    return QEMU_VIRT_VIRTIO_MMIO_SIZE;
}

uint64_t board_virtio_mmio_stride(void) {
    return QEMU_VIRT_VIRTIO_MMIO_STRIDE;
}

int board_map_mmio(uint64_t *pgd) {
    int status = vmm_map_page(pgd, QEMU_VIRT_UART0_BASE, QEMU_VIRT_UART0_BASE,
                              VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);

    if (status == 0) {
        status = vmm_map_range(pgd, QEMU_VIRT_GIC_DIST_BASE,
                               QEMU_VIRT_GIC_DIST_BASE,
                               QEMU_VIRT_GIC_MMIO_SIZE,
                               VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
    }

    if (status == 0) {
        status = vmm_map_range(pgd, QEMU_VIRT_VIRTIO_MMIO_BASE,
                               QEMU_VIRT_VIRTIO_MMIO_BASE,
                               QEMU_VIRT_VIRTIO_MMIO_SIZE,
                               VMM_FLAG_READ | VMM_FLAG_WRITE | VMM_FLAG_DEVICE);
    }

    return status;
}

uint32_t board_virtio_input_irq(void) {
    return QEMU_VIRT_VIRTIO_INPUT_IRQ;
}

int board_virtio_input_init(void) {
    uint64_t input_base;

    if (virtio_input_probe_range(QEMU_VIRT_VIRTIO_MMIO_BASE,
                                 QEMU_VIRT_VIRTIO_MMIO_SIZE,
                                 QEMU_VIRT_VIRTIO_MMIO_STRIDE,
                                 &input_base) != 0) {
        return -1;
    }

    return virtio_input_init(&g_input_dev, input_base);
}

int board_virtio_input_poll(void) {
    return virtio_input_poll(&g_input_dev);
}

uint32_t board_virtio_snd_irq(void) {
    return QEMU_VIRT_VIRTIO_SND_IRQ;
}

int board_virtio_snd_init(void) {
    uint64_t snd_base = QEMU_VIRT_VIRTIO_MMIO_BASE +
                        (4 * QEMU_VIRT_VIRTIO_MMIO_STRIDE);

    if (virtio_snd_probe(snd_base) != 0) {
        return -1;
    }

    return virtio_snd_init(&g_snd_dev, snd_base, QEMU_VIRT_VIRTIO_SND_IRQ);
}

int board_virtio_snd_write_samples(const int16_t *samples, uint32_t count) {
    return virtio_snd_write_samples(&g_snd_dev, samples, count);
}
