#include "input.h"

#include "uart/pl011.h"

static input_event_t g_event_queue[INPUT_EVENT_QUEUE_SIZE];
static uint32_t g_event_head = 0;
static uint32_t g_event_tail = 0;
static uint32_t g_event_count = 0;

void input_queue_init(void) {
    g_event_head = 0;
    g_event_tail = 0;
    g_event_count = 0;
}

int input_queue_push(const input_event_t *event) {
    if (g_event_count >= INPUT_EVENT_QUEUE_SIZE) {
        return -1;
    }

    g_event_queue[g_event_tail] = *event;
    g_event_tail = (g_event_tail + 1) % INPUT_EVENT_QUEUE_SIZE;
    g_event_count++;

    return 0;
}

int input_queue_poll(input_event_t *event) {
    if (g_event_count == 0) {
        return -1;
    }

    *event = g_event_queue[g_event_head];
    g_event_head = (g_event_head + 1) % INPUT_EVENT_QUEUE_SIZE;
    g_event_count--;

    return 0;
}

int input_queue_poll_char(void) {
    input_event_t event;

    if (input_queue_poll(&event) != 0) {
        return -1;
    }

    if (event.type != INPUT_EVENT_KEY_PRESS) {
        return -1;
    }

    return (int)(event.data.key.key & 0xff);
}

int input_queue_available(void) {
    return (int)g_event_count;
}

int input_uart_poll(void) {
    int c = uart_getc_nonblock();
    if (c < 0) {
        return -1;
    }

    input_event_t event = {
        .type = INPUT_EVENT_KEY_PRESS,
        .timestamp = 0,
        .data.key.key = (uint32_t)c
    };

    return input_queue_push(&event);
}
