#include "kernel/process.h"

#include "kernel/mm/mmu.h"

void process_activate_context(const process_t *process, exception_frame_t *frame) {
    if (process == 0 || frame == 0) {
        return;
    }

    if (process->page_table != 0) {
        mmu_set_ttbr0(process->page_table);
    }

    process_load_context(process, frame);
}
