#ifndef KOLIBRIARM_KERNEL_USER_DEMO_H
#define KOLIBRIARM_KERNEL_USER_DEMO_H

#include <stdint.h>

typedef int (*user_demo_map_mmio_fn_t)(uint64_t *pgd);

uint64_t user_demo_run(uint64_t memory_base, uint64_t memory_size,
                       user_demo_map_mmio_fn_t map_mmio);
int user_demo_spawn_vfs(const char *path, uint32_t entry_index,
                        const uint64_t *argv_ptr, uint32_t argc);
uint64_t user_demo_return_address(void);

#endif
