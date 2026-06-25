// KolibriARM app: clock (C version, on programs/libkarm + libkarmdesk)
//
// Creates a window that displays the current uptime as HH:MM:SS, reads
// timer ticks via SYS_TIMEINFO, and redraws once per second using a
// yield-based delay. 'q' closes the app; a click on the kernel-drawn
// close box fires GUI_EVENT_CLOSE and exits cleanly.
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

static void write_cstr(long fd, const char *s) {
    while (*s) {
        (void)kli_write((int)fd, s, 1);
        s++;
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

static void redraw(long wid) {
    uint64_t info[3];
    if (kli_timeinfo(info) < 0) {
        return;
    }
    char text[9];
    format_hhmmss(info[0], text);

    (void)gui_window_draw_rect(wid, 20, 30, WIN_W - 40, 28, 0xff202830ULL);
    (void)gui_window_draw_text(wid, 40, 34, 0xffe0e8f0ULL, text);
    (void)gui_window_flush(wid, 0, 0, WIN_W, WIN_H - TITLE_BAR_H);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write_cstr(1, "clock: starting\n");

    long wid = gui_window_create(440, 64, WIN_W, WIN_H,
                                 0xff202830LL, 0xff808080LL, "clock");
    if (wid < 0) {
        write_cstr(1, "clock: window create failed\n");
        return 1;
    }
    (void)gui_window_set_title(wid, "clock", TITLE_BAR_H);

    gui_event_t events[EVENT_CAP];

    for (;;) {
        // Yield ~200 times so other apps get a fair share of the CPU
        // and ~1 second passes between redraws.
        for (int i = 0; i < YIELDS_PER_SEC; i++) {
            (void)kli_yield();
        }

        redraw(wid);

        // Drain pending events; close on GUI_EVENT_CLOSE.
        long n = gui_window_event(wid, events, EVENT_CAP);
        if (n > 0) {
            for (long i = 0; i < n; i++) {
                if (events[i].type == GUI_EVENT_CLOSE) {
                    (void)gui_window_destroy(wid);
                    return 0;
                }
            }
        }
    }
}
