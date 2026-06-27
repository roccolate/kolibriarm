#ifndef KOLIBRIARM_KERNEL_PROCESS_H
#define KOLIBRIARM_KERNEL_PROCESS_H

/*
 * Process-control block API.
 *
 * PIDs are non-zero and unique inside the fixed process table. User regions
 * are half-open virtual ranges [start, end); entries with OWNED_PAGES carry
 * PMM-backed pages that process_free_resources/process_release must return.
 * Region helpers reject zero-length and overflowing ranges, while
 * process_user_range_contains treats a zero-length query as vacuously valid for
 * a non-null process.
 */

#include <stdint.h>

#include "kernel/exceptions.h"

#define PROCESS_MAX_PROCESSES    16U
#define PROCESS_MAX_USER_REGIONS 8U
#define PROCESS_NAME_LEN         16U
#define PROCESS_USER_MMAP_BASE   0x0000000100000000ULL
#define PROCESS_USER_MMAP_LIMIT  0x0000000200000000ULL
#define PROCESS_USER_REGION_OWNED_PAGES (1ULL << 0)

typedef enum {
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE,
} process_state_t;

typedef enum {
    PROCESS_DISPATCH_EXIT = 0,
    PROCESS_DISPATCH_PREEMPT = 1,
} process_dispatch_policy_t;

typedef struct {
    uint64_t start;
    uint64_t end;
    uint64_t paddr;
    uint64_t flags;
} process_user_region_t;

typedef struct process {
    uint32_t pid;
    const char *name;
    char name_storage[PROCESS_NAME_LEN];
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
    uint64_t *page_table;
    process_state_t state;
    uint64_t exit_code;
    uint64_t next_user_vaddr;
    process_user_region_t user_regions[PROCESS_MAX_USER_REGIONS];
    uint32_t user_region_count;
} process_t;

void process_set_current(process_t *process);
process_t *process_current(void);
void process_table_init(void);
process_t *process_alloc(uint32_t pid, const char *name);
/*
 * Release a process slot. Frees the process-owned physical pages
 * (anonymous mmap regions + the page-table page itself) before
 * zeroing the slot. Does NOT destroy GUI windows owned by the
 * process — call gui_destroy_windows_for_pid(pid) first if the
 * process owned any. Idempotent: a process whose slot is already
 * UNUSED is a no-op.
 */
void process_release(process_t *process);
/*
 * Walk a process's user_regions and free any page-table pages and
 * anonymous-mapped pages owned by it. Safe to call multiple times:
 * regions whose owned pages have already been released are skipped
 * via PROCESS_USER_REGION_OWNED_PAGES. Cleared regions have
 * paddr==0 and are skipped. After return the process owns no
 * physical pages and process->page_table is 0.
 */
void process_free_resources(process_t *process);
uint32_t process_count(void);
const process_t *process_at(uint32_t index);
int process_index(const process_t *process, uint32_t *index);
process_t *process_find(uint32_t pid);
process_t *process_next_runnable(process_t *after);
int process_dispatch_next(process_t *current, exception_frame_t *frame,
                          process_dispatch_policy_t policy);
void process_reclaim_zombies(void);
int process_wait_zombie(uint32_t pid, uint64_t *exit_code);
int process_kill(uint32_t pid, uint64_t exit_code);
void process_init(process_t *process, uint32_t pid, const char *name);
void process_set_entry(process_t *process, uint64_t pc, uint64_t sp,
                       uint64_t pstate);
void process_save_context(process_t *process, const uint64_t regs[31],
                          uint64_t pc, uint64_t pstate, uint64_t sp);
void process_load_context(const process_t *process, exception_frame_t *frame);

static inline void process_activate_context(const process_t *process,
                                            exception_frame_t *frame) {
    if (process == 0 || frame == 0) {
        return;
    }
    if (process->page_table != 0) {
        extern void mmu_set_ttbr0(uint64_t *table);
        mmu_set_ttbr0(process->page_table);
    }
    process_load_context(process, frame);
}
void process_set_page_table(process_t *process, uint64_t *page_table);
void process_mark_exited(process_t *process, uint64_t exit_code);
int process_add_user_region(process_t *process, uint64_t start, uint64_t size);
int process_alloc_user_region(process_t *process, uint64_t size, uint64_t *addr);
int process_set_user_region_mapping(process_t *process, uint64_t start,
                                    uint64_t size, uint64_t paddr,
                                    uint64_t flags);
int process_find_user_region(const process_t *process, uint64_t start,
                             uint64_t size, process_user_region_t *out);
int process_remove_user_region(process_t *process, uint64_t start,
                               uint64_t size);
int process_remove_user_region_info(process_t *process, uint64_t start,
                                    uint64_t size,
                                    process_user_region_t *removed);
int process_user_range_contains(const process_t *process, uint64_t start,
                                uint64_t size);

#endif
