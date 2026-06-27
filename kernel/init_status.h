#ifndef KOLIBRIARM_KERNEL_INIT_STATUS_H
#define KOLIBRIARM_KERNEL_INIT_STATUS_H

#include <stdint.h>

/*
 * Boot-phase status table.
 *
 * kernel_main records each major init phase here and the interactive console
 * reads the table through init_status_at. The table is fixed-size and keeps one
 * status byte per phase; callers should treat invalid phases as programming
 * errors, not dynamic runtime input.
 */

typedef enum {
    INIT_PHASE_BOARD = 0,
    INIT_PHASE_DTB,
    INIT_PHASE_PMM,
    INIT_PHASE_KHEAP,
    INIT_PHASE_VMM,
    INIT_PHASE_CONSOLE,
    INIT_PHASE_VFS,
    INIT_PHASE_IRQ_TIMER,
    INIT_PHASE_STORAGE,
    INIT_PHASE_DISPLAY,
    INIT_PHASE_NETWORK,
    INIT_PHASE_INPUT,
    INIT_PHASE_PANEL,
    INIT_PHASE_SCHED,
    INIT_PHASE_COUNT,
} init_phase_t;

typedef enum {
    INIT_STATUS_SKIPPED = 0,
    INIT_STATUS_OK,
    INIT_STATUS_WARN,
    INIT_STATUS_FAIL,
} init_status_t;

typedef struct {
    const char *name;
    init_status_t status;
} init_status_entry_t;

void init_status_reset(void);
void init_status_set(init_phase_t phase, init_status_t status);
init_status_t init_status_get(init_phase_t phase);
const init_status_entry_t *init_status_at(uint32_t index);
uint32_t init_status_count(void);
const char *init_status_label(init_status_t status);

#endif
