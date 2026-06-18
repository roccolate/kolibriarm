#ifndef KOLIBRIARM_DRIVERS_BOARD_H
#define KOLIBRIARM_DRIVERS_BOARD_H

#include <stdint.h>

const char *board_name(void);
void board_early_init(void);
int board_map_mmio(uint64_t *pgd);

void board_irq_init(void);
void board_irq_enable(uint32_t irq);
uint32_t board_irq_ack(void);
void board_irq_end(uint32_t irq);
int board_irq_is_spurious(uint32_t irq);

uint32_t board_uart0_irq(void);

uint64_t board_virtio_mmio_base(void);
uint64_t board_virtio_mmio_size(void);
uint64_t board_virtio_mmio_stride(void);

int board_emmc_read(uint32_t lba, uint32_t count, void *buffer);
int board_emmc_write(uint32_t lba, uint32_t count, const void *buffer);

int board_storage_read(uint32_t lba, uint32_t count, void *buffer);
int board_storage_write(uint32_t lba, uint32_t count, const void *buffer);
int board_storage_init(void);

uint32_t board_virtio_input_irq(void);
int board_virtio_input_init(void);
int board_virtio_input_poll(void);

uint32_t board_virtio_snd_irq(void);
int board_virtio_snd_init(void);
int board_virtio_snd_write_samples(const int16_t *samples, uint32_t count);

#endif
