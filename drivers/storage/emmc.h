#ifndef KOLIBRIARM_DRIVERS_STORAGE_EMMC_H
#define KOLIBRIARM_DRIVERS_STORAGE_EMMC_H

#include <stdint.h>

#define EMMC_BASE         0xFE340000ULL
#define EMMC_IRQ          134U

#define EMMC_BLKSZ        512U

typedef struct {
    uint64_t base;
    uint32_t clock_div;
    uint8_t ready;
    uint8_t high_cap;
} emmc_device_t;

int emmc_init(emmc_device_t *dev, uint64_t base);
int emmc_read_sector(emmc_device_t *dev, uint32_t lba, uint32_t count, void *buffer);
int emmc_write_sector(emmc_device_t *dev, uint32_t lba, uint32_t count, const void *buffer);

#endif