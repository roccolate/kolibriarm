#ifndef LWIP_CUSTOM_H
#define LWIP_CUSTOM_H

#include "kernel/mm/kheap.h"

#define mem_malloc(size) kmalloc(size)
#define mem_free(ptr) kfree(ptr)

#endif