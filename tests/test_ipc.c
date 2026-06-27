#include <stdint.h>

#include "unity/unity.h"
#include "../kernel/ipc.h"

void test_ipc_send_and_recv_message(void) {
    const uint8_t payload[] = { 'i', 'p', 'c' };
    ipc_message_t message;

    ipc_init();
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)ipc_send(1, 2, payload,
                                                sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)ipc_recv(2, &message));
    TEST_ASSERT_EQUAL_UINT64(1, message.sender_pid);
    TEST_ASSERT_EQUAL_UINT64(2, message.target_pid);
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload), message.size);
    TEST_ASSERT_EQUAL_UINT64('i', message.data[0]);
    TEST_ASSERT_EQUAL_UINT64('p', message.data[1]);
    TEST_ASSERT_EQUAL_UINT64('c', message.data[2]);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)ipc_recv(2, &message));
}

void test_ipc_keeps_messages_by_target_pid(void) {
    const uint8_t first[] = { 'a' };
    const uint8_t second[] = { 'b' };
    ipc_message_t message;

    ipc_init();
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)ipc_send(1, 3, first, sizeof(first)));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)ipc_send(2, 4, second,
                                                sizeof(second)));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)ipc_recv(4, &message));
    TEST_ASSERT_EQUAL_UINT64(2, message.sender_pid);
    TEST_ASSERT_EQUAL_UINT64('b', message.data[0]);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)ipc_recv(3, &message));
    TEST_ASSERT_EQUAL_UINT64(1, message.sender_pid);
    TEST_ASSERT_EQUAL_UINT64('a', message.data[0]);
}

void test_ipc_recv_zeroes_unused_payload_tail(void) {
    uint8_t full[IPC_MAX_MESSAGE_SIZE];
    const uint8_t short_payload[] = { 0x7e };
    ipc_message_t message;

    for (uint32_t i = 0; i < sizeof(full); i++) {
        full[i] = 0xffU;
        message.data[i] = 0xaaU;
    }

    ipc_init();
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)ipc_send(1, 2, full, sizeof(full)));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)ipc_recv(2, &message));
    TEST_ASSERT_EQUAL_UINT64(sizeof(full), message.size);

    for (uint32_t i = 0; i < sizeof(message.data); i++) {
        message.data[i] = 0xaaU;
    }

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)ipc_send(1, 2, short_payload,
                                                sizeof(short_payload)));
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)ipc_recv(2, &message));
    TEST_ASSERT_EQUAL_UINT64(sizeof(short_payload), message.size);
    TEST_ASSERT_EQUAL_UINT64(0x7eU, message.data[0]);
    for (uint32_t i = 1; i < IPC_MAX_MESSAGE_SIZE; i++) {
        TEST_ASSERT_EQUAL_UINT64(0, message.data[i]);
    }
}

void test_ipc_rejects_invalid_inputs_and_full_queue(void) {
    uint8_t payload[IPC_MAX_MESSAGE_SIZE + 1U];
    ipc_message_t message;

    for (uint32_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)i;
    }

    ipc_init();
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)ipc_send(0, 1, payload, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)ipc_send(1, 0, payload, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)ipc_send(1, 2, 0, 1));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)ipc_send(1, 2, payload, 0));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)ipc_send(1, 2, payload,
                                                sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)ipc_recv(0, &message));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1, (uint64_t)ipc_recv(1, 0));

    for (uint32_t i = 0; i < IPC_MAX_MESSAGES; i++) {
        TEST_ASSERT_EQUAL_UINT64(0,
                                 (uint64_t)ipc_send(1, 2, payload, 1));
    }
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)ipc_send(1, 2, payload, 1));
}
