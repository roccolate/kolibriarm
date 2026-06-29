// ArmoniOS app: clock (C version, on programs/libkarm + libkarmdesk)
//
// Creates a window that displays the current uptime as HH:MM:SS, reads
// timer ticks via SYS_TIMEINFO, and redraws once per second using a
// yield-based delay. 'q' closes the app; a click on the kernel-drawn
// close box fires GUI_EVENT_CLOSE and exits cleanly. Runtime buffers live in
// anonymous user memory so the app does not park event arrays at the top edge
// of the fixed 4 KB EL0 stack.
//
// Non-window syscalls (write, yield, timeinfo, exit) go through
// libkarm's typed wrappers in <libkarm/syscall.h>. Window syscalls
// (create, destroy, draw_text, draw_rect, set_title, event, flush)
// go through libkarmdesk's typed gui_* wrappers in
// <libkarmdesk/gui.h>.

#include <stdint.h>

#include "libkarm/syscall.h"
#include "libkarmdesk/gui.h"

// programs/apps/image.ld now also picks up the default .text* and
// .rodata* sections, so this file's C functions and string literals
// land in the flat image without an explicit section attribute.

#define WIN_W            200
#define WIN_H             80
#define TITLE_BAR_H       16
#define EVENT_CAP          4
#define YIELDS_PER_SEC   200

typedef struct {
    long wid;
    int wait;
    uint64_t info[3];
    char text[9];
    gui_event_t events[EVENT_CAP];
} clock_state_t;

static void zero_clock_state(clock_state_t *state) {
    uint8_t *bytes = (uint8_t *)(uintptr_t)state;

    for (uint32_t i = 0; i < sizeof(*state); i++) {
        bytes[i] = 0;
    }
}

static void format_hhmmss(uint64_t ticks, char *out) {
    // 100 Hz timer: seconds = ticks / 100.
    uint64_t total_seconds = ticks / 100ULL;
    uint64_t hh = total_seconds / 3600ULL;
    uint64_t mm = (total_seconds / 60ULL) % 60ULL;
    uint64_t ss = total_seconds % 60ULL;
    out[0] = (char)('0' + (int)(hh / 10U));
    out[1] = (char)('0' + (int)(hh % 10U));
    out[2] = ':';
    out[3] = (char)('0' + (int)(mm / 10U));
    out[4] = (char)('0' + (int)(mm % 10U));
    out[5] = ':';
    out[6] = (char)('0' + (int)(ss / 10U));
    out[7] = (char)('0' + (int)(ss % 10U));
    out[8] = '\0';
}

static void redraw(clock_state_t *state) {
    if (kli_timeinfo(state->info) < 0) {
        return;
    }
    format_hhmmss(state->info[0], state->text);

    (void)gui_window_draw_rect(state->wid, 20, 30, WIN_W - 40, 28,
                               0xff202830ULL);
    (void)gui_window_draw_text(state->wid, 40, 34, 0xffe0e8f0ULL,
                               state->text);
    (void)gui_window_flush(state->wid, 0, 0, WIN_W, WIN_H - TITLE_BAR_H);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    kli_write_cstr(1, "clock: starting\n");

    long state_addr = kli_mmap(0, sizeof(clock_state_t), 0);
    if (state_addr < 0) {
        kli_write_cstr(1, "clock: state mmap failed\n");
        return 1;
    }
    clock_state_t *state = (clock_state_t *)(uintptr_t)state_addr;
    zero_clock_state(state);
    state->wid = gui_window_create(440, 64, WIN_W, WIN_H,
                                   0xff202830LL, 0xff808080LL, "clock");
    if (state->wid < 0) {
        kli_write_cstr(1, "clock: window create failed\n");
        return 1;
    }
    (void)gui_window_set_title(state->wid, "clock", TITLE_BAR_H);

    state->wait = YIELDS_PER_SEC;
    redraw(state);

    for (;;) {
        // Drain pending events every yield so close / q are responsive.
        long n = gui_window_event(state->wid, state->events, EVENT_CAP);
        if (n > 0) {
            for (long i = 0; i < n; i++) {
                if (state->events[i].type == GUI_EVENT_CLOSE) {
                    (void)gui_window_destroy(state->wid);
                    return 0;
                }
                if (state->events[i].type == GUI_EVENT_KEY_PRESS &&
                    (state->events[i].data1 == 'q' ||
                     state->events[i].data1 == 'Q')) {
                    (void)gui_window_destroy(state->wid);
                    return 0;
                }
            }
        }

        state->wait--;
        if (state->wait <= 0) {
            redraw(state);
            state->wait = YIELDS_PER_SEC;
        }

        (void)kli_yield();
    }
}
