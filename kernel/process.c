/*
 * Process table, trap-frame state, and user-region ownership.
 *
 * This module is the kernel's process-control block owner. It tracks the
 * fixed process table, current process pointer, saved EL0 context, user
 * virtual-memory regions, and the physical pages each process must return
 * when it exits. Syscall code treats process_user_range_contains as the
 * user-pointer authority, so range arithmetic here must stay overflow-safe.
 */

#include "kernel/process.h"

#include <stdint.h>

#include "kernel/gui.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"

#define USER_REGION_ALIGN 4096ULL
_Static_assert((USER_REGION_ALIGN & (USER_REGION_ALIGN - 1ULL)) == 0,
               "user-region alignment must be a power of two");

static process_t g_processes[PROCESS_MAX_PROCESSES];
static process_t *g_current_process;
static uint32_t g_process_count;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ULL) & ~(alignment - 1ULL);
}

static int user_region_end(uint64_t start, uint64_t size, uint64_t *end) {
    if (size == 0 || end == 0 || start > UINT64_MAX - size) {
        return -1;
    }

    *end = start + size;
    return 0;
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

/*
 * Free every physical page the process owns: each anonymous-mapped
 * region's pages plus the page-table hierarchy itself. Called from
 * process_release so a slot, once reclaimed, returns all of its
 * PMM budget to the allocator. Safe to call multiple times:
 * PROCESS_USER_REGION_OWNED_PAGES is cleared as pages are released
 * and paddr is reset to 0, so a double-free would try to release
 * zero pages and the PMM layer rejects it.
 */
void process_free_resources(process_t *process) {
    if (process == 0) {
        return;
    }

    for (uint32_t i = 0; i < process->user_region_count &&
                        i < PROCESS_MAX_USER_REGIONS; i++) {
        process_user_region_t *region = &process->user_regions[i];
        if ((region->flags & PROCESS_USER_REGION_OWNED_PAGES) == 0 ||
            region->paddr == 0) {
            continue;
        }
        if (region->end > region->start) {
            uint64_t size = region->end - region->start;
            for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
                pmm_free_page(region->paddr + off);
            }
        }
        region->flags &= ~((uint64_t)PROCESS_USER_REGION_OWNED_PAGES);
        region->paddr = 0;
    }

    if (process->page_table != 0) {
        vmm_free_table(process->page_table);
        process->page_table = 0;
    }
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
    if (pid == 0 || process_find(pid) != 0) {
        return 0;
    }

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

    /*
     * If the slot has already been released, process_init left the
     * state at UNUSED and page_table at 0. Skip the PMM work and
     * just no-op so callers (e.g. process_reclaim_zombies running
     * on an already-cleaned slot) stay safe.
     */
    if (process->state == PROCESS_UNUSED && process->page_table == 0 &&
        process->user_region_count == 0) {
        return;
    }

    if (g_current_process == process) {
        g_current_process = 0;
    }

    int counted = process_in_table(process) && process->state != PROCESS_UNUSED;

    process_free_resources(process);
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

int process_dispatch_next(process_t *current, exception_frame_t *frame,
                          process_dispatch_policy_t policy) {
    process_t *next;

    if (frame == 0 ||
        (policy != PROCESS_DISPATCH_EXIT &&
         policy != PROCESS_DISPATCH_PREEMPT)) {
        return 0;
    }
    if (current == 0 && policy == PROCESS_DISPATCH_PREEMPT) {
        return 0;
    }

    next = process_next_runnable(current);
    if (next == 0) {
        return 0;
    }

    if (policy == PROCESS_DISPATCH_PREEMPT &&
        current != 0 && current->state == PROCESS_RUNNING) {
        current->state = PROCESS_READY;
    }

    next->state = PROCESS_RUNNING;
    process_set_current(next);
#ifdef KOLIBRIARM_TEST
    process_load_context(next, frame);
#else
    process_activate_context(next, frame);
#endif
    return 1;
}

int process_wait_zombie(uint32_t pid, uint64_t *exit_code) {
    process_t *process = process_find(pid);

    if (process == 0 || exit_code == 0 ||
        process == g_current_process ||
        process->state != PROCESS_ZOMBIE) {
        return -1;
    }

    *exit_code = process->exit_code;
    /* GUI windows were destroyed by process_mark_exited when the
     * process became zombie; process_release here only frees the
     * page table and mmap pages. */
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
            /*
             * Note: GUI windows were destroyed by
             * process_mark_exited when the process first became a
             * zombie. process_release here only needs to free
             * the page table and mmap pages.
             */
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

void process_set_entry(process_t *process, uint64_t pc, uint64_t sp,
                       uint64_t pstate) {
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

    /*
     * Centralised cleanup: every path that marks a process as a
     * zombie (handle_user_fault, sys_exit, process_kill) lands
     * here, and only here. Destroying the owned GUI windows at
     * this point keeps the desktop from accumulating ghost
     * windows the user cannot reach (the close-button path only
     * fires for live owners). The window destroy is a no-op for
     * processes that never opened a window (kernel threads,
     * short-lived helpers) so it is safe to do unconditionally.
     *
     * Both the window destroy and the exit_code write are gated
     * on `state != PROCESS_ZOMBIE` so a second call (e.g. a kill
     * followed by an unrelated sys_exit from another pid that
     * races) is a clean no-op and does not touch freed memory.
     */
    if (process->state == PROCESS_ZOMBIE) {
        return;
    }
    gui_destroy_windows_for_pid(gui_desktop(), process->pid);

    process->exit_code = exit_code;
    process->state = PROCESS_ZOMBIE;
}

int process_add_user_region(process_t *process, uint64_t start, uint64_t size) {
    uint64_t end;

    if (process == 0 ||
        process->user_region_count >= PROCESS_MAX_USER_REGIONS ||
        user_region_end(start, size, &end) != 0) {
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
    uint64_t end;

    if (process == 0 || addr == 0 || size == 0) {
        return -1;
    }

    aligned_size = align_up(size, USER_REGION_ALIGN);
    if (aligned_size < size) {
        return -1;
    }

    start = align_up(process->next_user_vaddr, USER_REGION_ALIGN);
    if (start < process->next_user_vaddr ||
        user_region_end(start, aligned_size, &end) != 0 ||
        end > PROCESS_USER_MMAP_LIMIT) {
        return -1;
    }

    if (process_add_user_region(process, start, aligned_size) != 0) {
        return -1;
    }

    process->next_user_vaddr = end;
    *addr = start;

    return 0;
}

int process_set_user_region_mapping(process_t *process, uint64_t start,
                                    uint64_t size, uint64_t paddr,
                                    uint64_t flags) {
    uint64_t end;

    if (process == 0 || user_region_end(start, size, &end) != 0) {
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

    if (process == 0 || user_region_end(start, size, &end) != 0) {
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

    if (process == 0 || user_region_end(start, size, &end) != 0) {
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

int process_remove_user_region(process_t *process, uint64_t start,
                               uint64_t size) {
    return process_remove_user_region_info(process, start, size, 0);
}

int process_user_range_contains(const process_t *process, uint64_t start,
                                uint64_t size) {
    uint64_t end;

    if (process == 0) {
        return 0;
    }

    if (size == 0) {
        return 1;
    }

    if (user_region_end(start, size, &end) != 0) {
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
