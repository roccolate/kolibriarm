#ifndef KOLIBRIARM_KERNEL_IPC_H
#define KOLIBRIARM_KERNEL_IPC_H

#include <stdint.h>

#define IPC_MAX_MESSAGES 8U
#define IPC_MAX_MESSAGE_SIZE 16U

/*
 * Small process-to-process message ABI.
 *
 * The kernel queue stores fixed-size payload buffers, but only the first
 * `size` bytes are meaningful. ipc_recv zeroes the remaining data bytes so
 * receivers can safely inspect the full struct in tests and diagnostics.
 */

typedef struct {
    uint32_t sender_pid;
    uint32_t target_pid;
    uint32_t size;
    uint8_t data[IPC_MAX_MESSAGE_SIZE];
} ipc_message_t;

void ipc_init(void);
int ipc_send(uint32_t sender_pid, uint32_t target_pid, const uint8_t *data,
             uint32_t size);
int ipc_recv(uint32_t target_pid, ipc_message_t *message);

#endif
