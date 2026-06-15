#ifndef KOLIBRIARM_KERNEL_PROCESS_H
#define KOLIBRIARM_KERNEL_PROCESS_H

#include <stdint.h>

#include "kernel/exceptions.h"

#define PROCESS_MAX_PROCESSES    4U
#define PROCESS_MAX_USER_REGIONS 4U
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

typedef struct {
    uint64_t start;
    uint64_t end;
    uint64_t paddr;
    uint64_t flags;
} process_user_region_t;

typedef struct process {
    uint32_t pid;
    const char *name;
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
    struct process *next;
} process_t;

void process_set_current(process_t *process);
process_t *process_current(void);
void process_table_init(void);
process_t *process_alloc(uint32_t pid, const char *name);
void process_release(process_t *process);
uint32_t process_count(void);
process_t *process_next_runnable(process_t *after);
uint32_t process_reclaim_zombies(void);
void process_init(process_t *process, uint32_t pid, const char *name);
void process_set_entry(process_t *process, uint64_t pc, uint64_t sp, uint64_t pstate);
void process_save_context(process_t *process, const uint64_t regs[31],
                          uint64_t pc, uint64_t pstate, uint64_t sp);
void process_load_context(const process_t *process, exception_frame_t *frame);
void process_activate_context(const process_t *process, exception_frame_t *frame);
void process_set_page_table(process_t *process, uint64_t *page_table);
void process_mark_exited(process_t *process, uint64_t exit_code);
int process_add_user_region(process_t *process, uint64_t start, uint64_t size);
int process_alloc_user_region(process_t *process, uint64_t size, uint64_t *addr);
int process_set_user_region_mapping(process_t *process, uint64_t start,
                                    uint64_t size, uint64_t paddr,
                                    uint64_t flags);
int process_find_user_region(const process_t *process, uint64_t start,
                             uint64_t size, process_user_region_t *out);
int process_remove_user_region(process_t *process, uint64_t start, uint64_t size);
int process_remove_user_region_info(process_t *process, uint64_t start,
                                    uint64_t size,
                                    process_user_region_t *removed);
int process_user_range_contains(const process_t *process, uint64_t start, uint64_t size);

#endif
