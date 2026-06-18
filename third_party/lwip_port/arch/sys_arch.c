#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/sys.h"

#include "kernel/mm/kheap.h"

u32_t sys_now(void) {
    return 0;
}

err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
    (void)sem;
    (void)count;
    return ERR_OK;
}

void sys_sem_signal(sys_sem_t *sem) {
    (void)sem;
}

void sys_sem_free(sys_sem_t *sem) {
    (void)sem;
}

err_t sys_mutex_new(sys_mutex_t *mutex) {
    (void)mutex;
    return ERR_OK;
}

void sys_mutex_lock(sys_mutex_t *mutex) {
    (void)mutex;
}

void sys_mutex_unlock(sys_mutex_t *mutex) {
    (void)mutex;
}