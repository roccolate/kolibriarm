#include "kernel/init_status.h"

#define INIT_STATUS_NAME_BOARD     "board"
#define INIT_STATUS_NAME_DTB       "dtb"
#define INIT_STATUS_NAME_PMM       "pmm"
#define INIT_STATUS_NAME_KHEAP     "kheap"
#define INIT_STATUS_NAME_VMM       "vmm"
#define INIT_STATUS_NAME_CONSOLE   "console"
#define INIT_STATUS_NAME_VFS       "vfs"
#define INIT_STATUS_NAME_IRQ_TIMER "irq/timer"
#define INIT_STATUS_NAME_STORAGE   "storage"
#define INIT_STATUS_NAME_DISPLAY   "display"
#define INIT_STATUS_NAME_NETWORK   "network"
#define INIT_STATUS_NAME_INPUT     "input"
#define INIT_STATUS_NAME_PANEL     "panel"
#define INIT_STATUS_NAME_SCHED     "sched"

enum {
    INIT_STATUS_NAME_OFFSET_BOARD = 0,
    INIT_STATUS_NAME_OFFSET_DTB =
        INIT_STATUS_NAME_OFFSET_BOARD + sizeof(INIT_STATUS_NAME_BOARD),
    INIT_STATUS_NAME_OFFSET_PMM =
        INIT_STATUS_NAME_OFFSET_DTB + sizeof(INIT_STATUS_NAME_DTB),
    INIT_STATUS_NAME_OFFSET_KHEAP =
        INIT_STATUS_NAME_OFFSET_PMM + sizeof(INIT_STATUS_NAME_PMM),
    INIT_STATUS_NAME_OFFSET_VMM =
        INIT_STATUS_NAME_OFFSET_KHEAP + sizeof(INIT_STATUS_NAME_KHEAP),
    INIT_STATUS_NAME_OFFSET_CONSOLE =
        INIT_STATUS_NAME_OFFSET_VMM + sizeof(INIT_STATUS_NAME_VMM),
    INIT_STATUS_NAME_OFFSET_VFS =
        INIT_STATUS_NAME_OFFSET_CONSOLE + sizeof(INIT_STATUS_NAME_CONSOLE),
    INIT_STATUS_NAME_OFFSET_IRQ_TIMER =
        INIT_STATUS_NAME_OFFSET_VFS + sizeof(INIT_STATUS_NAME_VFS),
    INIT_STATUS_NAME_OFFSET_STORAGE =
        INIT_STATUS_NAME_OFFSET_IRQ_TIMER + sizeof(INIT_STATUS_NAME_IRQ_TIMER),
    INIT_STATUS_NAME_OFFSET_DISPLAY =
        INIT_STATUS_NAME_OFFSET_STORAGE + sizeof(INIT_STATUS_NAME_STORAGE),
    INIT_STATUS_NAME_OFFSET_NETWORK =
        INIT_STATUS_NAME_OFFSET_DISPLAY + sizeof(INIT_STATUS_NAME_DISPLAY),
    INIT_STATUS_NAME_OFFSET_INPUT =
        INIT_STATUS_NAME_OFFSET_NETWORK + sizeof(INIT_STATUS_NAME_NETWORK),
    INIT_STATUS_NAME_OFFSET_PANEL =
        INIT_STATUS_NAME_OFFSET_INPUT + sizeof(INIT_STATUS_NAME_INPUT),
    INIT_STATUS_NAME_OFFSET_SCHED =
        INIT_STATUS_NAME_OFFSET_PANEL + sizeof(INIT_STATUS_NAME_PANEL),
};

/*
 * Compact phase-name table.
 *
 * init_status_at returns one transient descriptor backed by this packed string
 * table. Offsets are derived from the literals above so adding a phase cannot
 * silently desynchronize the name table and the enum.
 */
static const char g_init_status_names[] =
    INIT_STATUS_NAME_BOARD "\0"
    INIT_STATUS_NAME_DTB "\0"
    INIT_STATUS_NAME_PMM "\0"
    INIT_STATUS_NAME_KHEAP "\0"
    INIT_STATUS_NAME_VMM "\0"
    INIT_STATUS_NAME_CONSOLE "\0"
    INIT_STATUS_NAME_VFS "\0"
    INIT_STATUS_NAME_IRQ_TIMER "\0"
    INIT_STATUS_NAME_STORAGE "\0"
    INIT_STATUS_NAME_DISPLAY "\0"
    INIT_STATUS_NAME_NETWORK "\0"
    INIT_STATUS_NAME_INPUT "\0"
    INIT_STATUS_NAME_PANEL "\0"
    INIT_STATUS_NAME_SCHED;

_Static_assert(sizeof(g_init_status_names) <= 256U,
               "init status name offsets must fit in uint8_t");

static const uint8_t g_init_status_name_offsets[INIT_PHASE_COUNT] = {
    [INIT_PHASE_BOARD] = INIT_STATUS_NAME_OFFSET_BOARD,
    [INIT_PHASE_DTB] = INIT_STATUS_NAME_OFFSET_DTB,
    [INIT_PHASE_PMM] = INIT_STATUS_NAME_OFFSET_PMM,
    [INIT_PHASE_KHEAP] = INIT_STATUS_NAME_OFFSET_KHEAP,
    [INIT_PHASE_VMM] = INIT_STATUS_NAME_OFFSET_VMM,
    [INIT_PHASE_CONSOLE] = INIT_STATUS_NAME_OFFSET_CONSOLE,
    [INIT_PHASE_VFS] = INIT_STATUS_NAME_OFFSET_VFS,
    [INIT_PHASE_IRQ_TIMER] = INIT_STATUS_NAME_OFFSET_IRQ_TIMER,
    [INIT_PHASE_STORAGE] = INIT_STATUS_NAME_OFFSET_STORAGE,
    [INIT_PHASE_DISPLAY] = INIT_STATUS_NAME_OFFSET_DISPLAY,
    [INIT_PHASE_NETWORK] = INIT_STATUS_NAME_OFFSET_NETWORK,
    [INIT_PHASE_INPUT] = INIT_STATUS_NAME_OFFSET_INPUT,
    [INIT_PHASE_PANEL] = INIT_STATUS_NAME_OFFSET_PANEL,
    [INIT_PHASE_SCHED] = INIT_STATUS_NAME_OFFSET_SCHED,
};

static uint8_t g_init_status_values[INIT_PHASE_COUNT];
static init_status_entry_t g_init_status_entry;

void init_status_reset(void) {
    for (uint32_t i = 0; i < INIT_PHASE_COUNT; i++) {
        g_init_status_values[i] = INIT_STATUS_SKIPPED;
    }
}

void init_status_set(init_phase_t phase, init_status_t status) {
    if ((uint32_t)phase >= INIT_PHASE_COUNT) {
        return;
    }

    g_init_status_values[phase] = status;
}

init_status_t init_status_get(init_phase_t phase) {
    if ((uint32_t)phase >= INIT_PHASE_COUNT) {
        return INIT_STATUS_FAIL;
    }

    return (init_status_t)g_init_status_values[phase];
}

const init_status_entry_t *init_status_at(uint32_t index) {
    if (index >= INIT_PHASE_COUNT) {
        return 0;
    }

    g_init_status_entry.name =
        &g_init_status_names[g_init_status_name_offsets[index]];
    g_init_status_entry.status = (init_status_t)g_init_status_values[index];
    return &g_init_status_entry;
}

uint32_t init_status_count(void) {
    return INIT_PHASE_COUNT;
}

const char *init_status_label(init_status_t status) {
    switch (status) {
    case INIT_STATUS_OK:
        return "OK";
    case INIT_STATUS_WARN:
        return "WARN";
    case INIT_STATUS_FAIL:
        return "FAIL";
    case INIT_STATUS_SKIPPED:
    default:
        return "SKIPPED";
    }
}
