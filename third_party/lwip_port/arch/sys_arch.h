#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include <stdint.h>

#define SYS_MBOX_NULL NULL
#define SYS_SEM_NULL  NULL

typedef uint32_t sys_sem_t;
typedef uint32_t sys_mbox_t;
typedef uint32_t sys_mutex_t;
typedef int32_t sys_thread_t;

#define sys_sem_valid(sem) ((sem) != 0)
#define sys_sem_set_invalid(sem) do { *(sem) = 0; } while(0)
#define sys_mbox_valid(mbox) ((mbox) != 0)
#define sys_mbox_set_invalid(mbox) do { *(mbox) = 0; } while(0)
#define sys_mutex_valid(mu) ((mu) != 0)
#define sys_mutex_set_invalid(mu) do { *(mu) = 0; } while(0)

#endif