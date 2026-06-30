#include "storage/emmc.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/kernel_compiler.h"

#define EMMC_ARG2    0x00
#define EMMC_BLKSZ_REG   0x04
#define EMMC_CMD     0x08
#define EMMC_RESP0   0x10
#define EMMC_RESP1   0x14
#define EMMC_RESP2   0x18
#define EMMC_RESP3   0x1C
#define EMMC_DATA    0x20
#define EMMC_STATUS  0x24
#define EMMC_CONTROL0 0x28
#define EMMC_CONTROL1 0x2C
#define EMMC_INTERRUPT 0x30
#define EMMC_MCLK_CTRL 0x38
#define EMMC_GP_OUT  0x40

#define EMMC_CONTROL0_USE_8BIT  (1U << 5)
#define EMMC_CONTROL0_HCTL_DW   (1U << 1)
#define EMMC_CONTROL0_HCTL_4   (1U << 0)

#define EMMC_CONTROL1_SRST      (1U << 0)
#define EMMC_CONTROL1_HCTL      (1U << 1)
#define EMMC_CONTROL1_CLK_INTLEN (1U << 2)
#define EMMC_CONTROL1_EMMC_RST  (1U << 0)

#define EMMC_INTERRUPT_ERROR_MASK 0x0178
#define EMMC_INTERRUPT_DATA_FLAGS 0x1F

#define EMMC_STATUS_CMD_INHIBIT (1U << 0)
#define EMMC_STATUS_DATA_INHIBIT (1U << 1)
#define EMMC_STATUS_SRST        (1U << 2)
#define EMMC_STATUS_IDLE        (1U << 3)

#define EMMC_CMD_RESP_NONE      0x00000000U
#define EMMC_CMD_RESP_R1        0x00020000U
#define EMMC_CMD_RESP_R2        0x00040000U
#define EMMC_CMD_RESP_R6        0x00020000U
#define EMMC_CMD_RESP_R7        0x00020000U

#define EMMC_CMD_NORMAL         0x00000000U
#define EMMC_CMD_ABORT          0x00000001U
#define EMMC_CMD_PENDING        0x00000003U

#define EMMC_CMD_INDEX_SHIFT    24
#define EMMC_CMD_TYPE_SHIFT     22
#define EMMC_CMD_TYPE_NORMAL    0U
#define EMMC_CMD_TYPE_SUSPEND   1U
#define EMMC_CMD_TYPE_RESUME    2U
#define EMMC_CMD_TYPE_ABORT     3U

#define EMMC_R1_ERRORS          0x017A0000U

#define EMMC_BLOCK_SIZE         512U

#define EMMC_VOLTAGE_SUPPLY     0x00CF0000U
#define EMMC_CMD_GO_IDLE        ((0U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_NONE)
#define EMMC_CMD_MMC_SEND_OP    ((1U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R2)
#define EMMC_CMD_ALL_SEND_CID   ((2U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R2 | 0x00010000U)
#define EMMC_CMD_SEND_REL_ADDR  ((3U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R6 | 0x00010000U)
#define EMMC_CMD_CARD_SELECT    ((7U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R1 | 0x00010000U)
#define EMMC_CMD_SEND_CSD       ((9U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R2)
#define EMMC_CMD_READ_SINGLE    ((17U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R1 | 0x00220020U)
#define EMMC_CMD_READ_MULTI     ((18U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R1 | 0x00220020U)
#define EMMC_CMD_WRITE_SINGLE   ((24U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R1 | 0x00220000U)
#define EMMC_CMD_WRITE_MULTI    ((25U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R1 | 0x00220000U)
#define EMMC_CMD_APP_CMD        ((55U << EMMC_CMD_INDEX_SHIFT) | EMMC_CMD_RESP_R1)

#define EMMC_ACMD_SD_SEND_OP    0x02020000U
#define EMMC_ACMD_SET_BUS_WIDTH 0x06020000U
#define EMMC_ACMD_SD_SEND_SCR   0x03220080U

#define EMMC_BUS_WIDTH_1        0U
#define EMMC_BUS_WIDTH_4        2U

static volatile uint32_t *emmc_reg(uint64_t base, uint32_t offset) {
    return (volatile uint32_t *)(uintptr_t)(base + offset);
}

static void emmc_barrier(void) {
#if defined(__aarch64__)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __sync_synchronize();
#endif
}

static int emmc_wait_status(emmc_device_t *dev, uint32_t mask, uint32_t timeout) {
    volatile uint32_t *status_reg = emmc_reg(dev->base, EMMC_STATUS);

    for (uint32_t i = 0; i < timeout; i++) {
        emmc_barrier();
        uint32_t status = *status_reg;
        if ((status & mask) == 0) {
            return 0;
        }
        for (volatile uint32_t j = 0; j < 1000; j++) {
            __asm__ volatile("nop");
        }
    }

    return -1;
}

static int emmc_send_command(emmc_device_t *dev, uint32_t cmd, uint32_t arg) {
    volatile uint32_t *cmd_reg = emmc_reg(dev->base, EMMC_CMD);

    if (emmc_wait_status(dev, EMMC_STATUS_CMD_INHIBIT, 100000) != 0) {
        return -1;
    }

    *emmc_reg(dev->base, EMMC_ARG2) = arg;
    emmc_barrier();
    *cmd_reg = cmd;
    emmc_barrier();

    if (emmc_wait_status(dev, EMMC_STATUS_CMD_INHIBIT, 100000) != 0) {
        return -2;
    }

    return 0;
}

KERNEL_UNUSED static uint32_t emmc_get_response(emmc_device_t *dev, uint32_t idx) {
    return *emmc_reg(dev->base, EMMC_RESP0 + idx * 4);
}

KERNEL_UNUSED static int emmc_set_bus_width(emmc_device_t *dev, uint32_t width) {
    uint32_t ctrl = *emmc_reg(dev->base, EMMC_CONTROL0);

    if (width == EMMC_BUS_WIDTH_4) {
        ctrl |= EMMC_CONTROL0_HCTL_DW;
    } else {
        ctrl &= ~EMMC_CONTROL0_HCTL_DW;
    }

    *emmc_reg(dev->base, EMMC_CONTROL0) = ctrl;
    emmc_barrier();

    return 0;
}

int emmc_init(emmc_device_t *dev, uint64_t base) {
    volatile uint32_t *ctrl1 = emmc_reg(base, EMMC_CONTROL1);

    *ctrl1 = EMMC_CONTROL1_SRST;
    emmc_barrier();

    for (volatile uint32_t i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }

    *ctrl1 = 0;
    emmc_barrier();

    for (volatile uint32_t i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }

    *ctrl1 = EMMC_CONTROL1_HCTL | EMMC_CONTROL1_CLK_INTLEN;
    emmc_barrier();

    *emmc_reg(base, EMMC_MCLK_CTRL) = 0x00000006U;
    emmc_barrier();

    for (volatile uint32_t i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }

    dev->base = base;
    dev->clock_div = 0;
    dev->high_cap = 0;
    dev->ready = 1;

    return 0;
}

int emmc_read_sector(emmc_device_t *dev, uint32_t lba, uint32_t count, void *buffer) {
    volatile uint32_t *blksz = emmc_reg(dev->base, EMMC_BLKSZ_REG);
    volatile uint32_t *data = emmc_reg(dev->base, EMMC_DATA);
    uint32_t *buf = (uint32_t *)buffer;

    if (!dev->ready) {
        return -1;
    }

    *blksz = EMMC_BLOCK_SIZE;
    emmc_barrier();

    if (emmc_send_command(dev, EMMC_CMD_READ_SINGLE, lba) != 0) {
        return -2;
    }

    for (uint32_t sector = 0; sector < count; sector++) {
        if (emmc_wait_status(dev, EMMC_STATUS_DATA_INHIBIT, 100000) != 0) {
            return -3;
        }

        for (uint32_t word = 0; word < EMMC_BLOCK_SIZE / 4; word++) {
            emmc_barrier();
            buf[sector * (EMMC_BLOCK_SIZE / 4) + word] = *data;
        }
    }

    return 0;
}

int emmc_write_sector(emmc_device_t *dev, uint32_t lba, uint32_t count, const void *buffer) {
    volatile uint32_t *blksz = emmc_reg(dev->base, EMMC_BLKSZ_REG);
    volatile uint32_t *data = emmc_reg(dev->base, EMMC_DATA);
    const uint32_t *buf = (const uint32_t *)buffer;

    if (!dev->ready) {
        return -1;
    }

    *blksz = EMMC_BLOCK_SIZE;
    emmc_barrier();

    if (emmc_send_command(dev, EMMC_CMD_WRITE_SINGLE, lba) != 0) {
        return -2;
    }

    for (uint32_t sector = 0; sector < count; sector++) {
        if (emmc_wait_status(dev, EMMC_STATUS_DATA_INHIBIT, 100000) != 0) {
            return -3;
        }

        for (uint32_t word = 0; word < EMMC_BLOCK_SIZE / 4; word++) {
            *data = buf[sector * (EMMC_BLOCK_SIZE / 4) + word];
            emmc_barrier();
        }
    }

    return 0;
}
