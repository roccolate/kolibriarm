#include "kernel/ipc.h"

#include <stdint.h>

/*
 * Fixed-size in-kernel IPC queue.
 *
 * Messages are copied into kernel-owned slots at send time and removed on the
 * first matching receive. Bytes beyond message.size are kept zero so callers
 * never observe stale payload tails after slot reuse.
 */

typedef struct {
    ipc_message_t message;
    uint8_t used;
} ipc_slot_t;

static ipc_slot_t g_ipc_slots[IPC_MAX_MESSAGES];

static void ipc_clear_slot(ipc_slot_t *slot) {
    if (slot == 0) {
        return;
    }

    slot->message.sender_pid = 0;
    slot->message.target_pid = 0;
    slot->message.size = 0;
    for (uint32_t i = 0; i < IPC_MAX_MESSAGE_SIZE; i++) {
        slot->message.data[i] = 0;
    }
    slot->used = 0;
}

void ipc_init(void) {
    for (uint32_t i = 0; i < IPC_MAX_MESSAGES; i++) {
        ipc_clear_slot(&g_ipc_slots[i]);
    }
}

int ipc_send(uint32_t sender_pid, uint32_t target_pid, const uint8_t *data,
             uint32_t size) {
    if (sender_pid == 0 || target_pid == 0 || data == 0 || size == 0 ||
        size > IPC_MAX_MESSAGE_SIZE) {
        return -1;
    }

    for (uint32_t i = 0; i < IPC_MAX_MESSAGES; i++) {
        ipc_slot_t *slot = &g_ipc_slots[i];

        if (slot->used == 0) {
            slot->message.sender_pid = sender_pid;
            slot->message.target_pid = target_pid;
            slot->message.size = size;
            for (uint32_t j = 0; j < size; j++) {
                slot->message.data[j] = data[j];
            }
            for (uint32_t j = size; j < IPC_MAX_MESSAGE_SIZE; j++) {
                slot->message.data[j] = 0;
            }
            slot->used = 1;
            return 0;
        }
    }

    return -1;
}

int ipc_recv(uint32_t target_pid, ipc_message_t *message) {
    if (target_pid == 0 || message == 0) {
        return -1;
    }

    for (uint32_t i = 0; i < IPC_MAX_MESSAGES; i++) {
        ipc_slot_t *slot = &g_ipc_slots[i];

        if (slot->used != 0 && slot->message.target_pid == target_pid) {
            message->sender_pid = slot->message.sender_pid;
            message->target_pid = slot->message.target_pid;
            message->size = slot->message.size;
            for (uint32_t j = 0; j < slot->message.size; j++) {
                message->data[j] = slot->message.data[j];
            }
            for (uint32_t j = slot->message.size; j < IPC_MAX_MESSAGE_SIZE;
                 j++) {
                message->data[j] = 0;
            }
            ipc_clear_slot(slot);
            return 0;
        }
    }

    return -1;
}
