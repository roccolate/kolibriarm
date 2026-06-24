#include "kernel/process.h"

#include <stdint.h>

#define USER_REGION_ALIGN 4096ULL

static process_t g_processes[PROCESS_MAX_PROCESSES];
static process_t *g_current_process;
static uint32_t g_process_count;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ULL) & ~(alignment - 1ULL);
}

static int process_in_table(const process_t *process) {
    uintptr_t addr = (uintptr_t)process;
    uintptr_t start = (uintptr_t)&g_processes[0];
    uintptr_t end = (uintptr_t)&g_processes[PROCESS_MAX_PROCESSES];

    return addr >= start && addr < end;
}

static void process_copy_name(process_t *process, const char *name) {
    uint32_t i = 0;

    if (process == 0) {
        return;
    }

    for (i = 0; i < PROCESS_NAME_LEN; i++) {
        process->name_storage[i] = '\0';
    }

    if (name != 0) {
        for (i = 0; i + 1U < PROCESS_NAME_LEN && name[i] != '\0'; i++) {
            process->name_storage[i] = name[i];
        }
    }

    process->name = process->name_storage;
}

static void region_clear(process_user_region_t *region) {
    if (region == 0) {
        return;
    }

    region->start = 0;
    region->end = 0;
    region->paddr = 0;
    region->flags = 0;
}

static void region_copy(process_user_region_t *dst,
                        const process_user_region_t *src) {
    if (dst == 0 || src == 0) {
        return;
    }

    dst->start = src->start;
    dst->end = src->end;
    dst->paddr = src->paddr;
    dst->flags = src->flags;
}

void process_set_current(process_t *process) {
    g_current_process = process;
}

process_t *process_current(void) {
    return g_current_process;
}

void process_table_init(void) {
    g_current_process = 0;
    g_process_count = 0;

    for (uint32_t i = 0; i < PROCESS_MAX_PROCESSES; i++) {
        process_init(&g_processes[i], 0, 0);
        g_processes[i].state = PROCESS_UNUSED;
    }
}

process_t *process_alloc(uint32_t pid, const char *name) {
    for (uint32_t i = 0; i < PROCESS_MAX_PROCESSES; i++) {
        if (g_processes[i].state == PROCESS_UNUSED) {
            process_init(&g_processes[i], pid, name);
            g_process_count++;
            return &g_processes[i];
        }
    }

    return 0;
}

void process_release(process_t *process) {
    if (process == 0) {
        return;
    }

    if (g_current_process == process) {
        g_current_process = 0;
    }

    int counted = process_in_table(process) && process->state != PROCESS_UNUSED;

    process_init(process, 0, 0);
    process->state = PROCESS_UNUSED;
    if (counted && g_process_count > 0) {
        g_process_count--;
    }
}

uint32_t process_count(void) {
    return g_process_count;
}

const process_t *process_at(uint32_t index) {
    if (index >= PROCESS_MAX_PROCESSES ||
        g_processes[index].state == PROCESS_UNUSED) {
        return 0;
    }

    return &g_processes[index];
}

int process_index(const process_t *process, uint32_t *index) {
    if (process == 0 || index == 0 || !process_in_table(process)) {
        return -1;
    }

    *index = (uint32_t)(process - g_processes);
    return 0;
}

process_t *process_find(uint32_t pid) {
    if (pid == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < PROCESS_MAX_PROCESSES; i++) {
        if (g_processes[i].state != PROCESS_UNUSED &&
            g_processes[i].pid == pid) {
            return &g_processes[i];
        }
    }

    return 0;
}

process_t *process_next_runnable(process_t *after) {
    uint32_t start = 0;
    uint32_t count = PROCESS_MAX_PROCESSES;

    if (after != 0 && process_in_table(after)) {
        start = (uint32_t)(after - g_processes) + 1U;
        count--;
    }

    for (uint32_t offset = 0; offset < count; offset++) {
        uint32_t index = (start + offset) % PROCESS_MAX_PROCESSES;

        if (g_processes[index].state == PROCESS_READY) {
            return &g_processes[index];
        }
    }

    return 0;
}

int process_wait_zombie(uint32_t pid, uint64_t *exit_code) {
    process_t *process = process_find(pid);

    if (process == 0 || exit_code == 0 ||
        process == g_current_process ||
        process->state != PROCESS_ZOMBIE) {
        return -1;
    }

    *exit_code = process->exit_code;
    process_release(process);
    return 0;
}

int process_kill(uint32_t pid, uint64_t exit_code) {
    process_t *process = process_find(pid);

    if (process == 0 || process == g_current_process ||
        process->state == PROCESS_ZOMBIE) {
        return -1;
    }

    process_mark_exited(process, exit_code);
    return 0;
}

void process_reclaim_zombies(void) {
    for (uint32_t i = 0; i < PROCESS_MAX_PROCESSES; i++) {
        if (g_processes[i].state == PROCESS_ZOMBIE) {
            process_release(&g_processes[i]);
        }
    }
}

void process_init(process_t *process, uint32_t pid, const char *name) {
    if (process == 0) {
        return;
    }

    process->pid = pid;
    process_copy_name(process, name);
    process->sp = 0;
    process->pc = 0;
    process->pstate = 0;
    process->page_table = 0;
    process->state = PROCESS_READY;
    process->exit_code = 0;
    process->next_user_vaddr = PROCESS_USER_MMAP_BASE;
    process->user_region_count = 0;

    for (uint32_t i = 0; i < 31; i++) {
        process->regs[i] = 0;
    }

    for (uint32_t i = 0; i < PROCESS_MAX_USER_REGIONS; i++) {
        region_clear(&process->user_regions[i]);
    }
}

void process_set_entry(process_t *process, uint64_t pc, uint64_t sp, uint64_t pstate) {
    if (process == 0) {
        return;
    }

    process->pc = pc;
    process->sp = sp;
    process->pstate = pstate;
}

void process_save_context(process_t *process, const uint64_t regs[31],
                          uint64_t pc, uint64_t pstate, uint64_t sp) {
    if (process == 0 || regs == 0) {
        return;
    }

    for (uint32_t i = 0; i < 31; i++) {
        process->regs[i] = regs[i];
    }

    process->pc = pc;
    process->pstate = pstate;
    process->sp = sp;
}

void process_load_context(const process_t *process, exception_frame_t *frame) {
    if (process == 0 || frame == 0) {
        return;
    }

    for (uint32_t i = 0; i < 31; i++) {
        frame->x[i] = process->regs[i];
    }

    frame->elr = process->pc;
    frame->spsr = process->pstate;
    frame->sp_el0 = process->sp;
}

void process_set_page_table(process_t *process, uint64_t *page_table) {
    if (process == 0) {
        return;
    }

    process->page_table = page_table;
}

void process_mark_exited(process_t *process, uint64_t exit_code) {
    if (process == 0) {
        return;
    }

    process->exit_code = exit_code;
    process->state = PROCESS_ZOMBIE;
}

int process_add_user_region(process_t *process, uint64_t start, uint64_t size) {
    uint64_t end;

    if (process == 0 || size == 0 ||
        process->user_region_count >= PROCESS_MAX_USER_REGIONS) {
        return -1;
    }

    end = start + size;
    if (end < start) {
        return -1;
    }

    for (uint32_t i = 0; i < process->user_region_count; i++) {
        const process_user_region_t *region = &process->user_regions[i];

        if (start < region->end && end > region->start) {
            return -1;
        }
    }

    process->user_regions[process->user_region_count].start = start;
    process->user_regions[process->user_region_count].end = end;
    process->user_regions[process->user_region_count].paddr = 0;
    process->user_regions[process->user_region_count].flags = 0;
    process->user_region_count++;

    return 0;
}

int process_alloc_user_region(process_t *process, uint64_t size, uint64_t *addr) {
    uint64_t aligned_size;
    uint64_t start;

    if (process == 0 || addr == 0 || size == 0) {
        return -1;
    }

    aligned_size = align_up(size, USER_REGION_ALIGN);
    if (aligned_size < size) {
        return -1;
    }

    start = align_up(process->next_user_vaddr, USER_REGION_ALIGN);
    if (start < process->next_user_vaddr ||
        start + aligned_size < start ||
        start + aligned_size > PROCESS_USER_MMAP_LIMIT) {
        return -1;
    }

    if (process_add_user_region(process, start, aligned_size) != 0) {
        return -1;
    }

    process->next_user_vaddr = start + aligned_size;
    *addr = start;

    return 0;
}

int process_set_user_region_mapping(process_t *process, uint64_t start,
                                    uint64_t size, uint64_t paddr,
                                    uint64_t flags) {
    uint64_t end;

    if (process == 0 || size == 0) {
        return -1;
    }

    end = start + size;
    if (end < start) {
        return -1;
    }

    for (uint32_t i = 0; i < process->user_region_count; i++) {
        process_user_region_t *region = &process->user_regions[i];

        if (region->start == start && region->end == end) {
            region->paddr = paddr;
            region->flags = flags;
            return 0;
        }
    }

    return -1;
}

int process_find_user_region(const process_t *process, uint64_t start,
                             uint64_t size, process_user_region_t *out) {
    uint64_t end;

    if (process == 0 || size == 0) {
        return -1;
    }

    end = start + size;
    if (end < start) {
        return -1;
    }

    for (uint32_t i = 0; i < process->user_region_count; i++) {
        const process_user_region_t *region = &process->user_regions[i];

        if (region->start == start && region->end == end) {
            region_copy(out, region);
            return 0;
        }
    }

    return -1;
}

int process_remove_user_region_info(process_t *process, uint64_t start,
                                    uint64_t size,
                                    process_user_region_t *removed) {
    uint64_t end;

    if (process == 0 || size == 0) {
        return -1;
    }

    end = start + size;
    if (end < start) {
        return -1;
    }

    for (uint32_t i = 0; i < process->user_region_count; i++) {
        process_user_region_t *region = &process->user_regions[i];

        if (region->start == start && region->end == end) {
            region_copy(removed, region);

            for (uint32_t j = i + 1; j < process->user_region_count; j++) {
                process->user_regions[j - 1] = process->user_regions[j];
            }

            region_clear(&process->user_regions[process->user_region_count - 1]);
            process->user_region_count--;
            return 0;
        }
    }

    return -1;
}

int process_remove_user_region(process_t *process, uint64_t start, uint64_t size) {
    return process_remove_user_region_info(process, start, size, 0);
}

int process_user_range_contains(const process_t *process, uint64_t start, uint64_t size) {
    uint64_t end;

    if (process == 0) {
        return 0;
    }

    if (size == 0) {
        return 1;
    }

    end = start + size;
    if (end < start) {
        return 0;
    }

    for (uint32_t i = 0; i < process->user_region_count; i++) {
        const process_user_region_t *region = &process->user_regions[i];

        if (start >= region->start && end <= region->end) {
            return 1;
        }
    }

    return 0;
}
