// KolibriARM app: clock (C version)
//
// Creates a window that displays the current uptime as HH:MM:SS, reads
// timer ticks via SYS_TIMEINFO, and redraws once per second using a
// yield-based delay. 'q' closes the app; a click on the kernel-drawn
// close box fires GUI_EVENT_CLOSE and exits cleanly.
//
// This is the first app migrated to programs/libkarm. Non-window
// syscalls (write, yield, timeinfo, exit) go through libkarm's typed
// wrappers in <libkarm/syscall.h>. Window syscalls (create, destroy,
// draw_text, draw_rect, set_title, event, flush) still go through
// libkarm's raw __syscallN trampolines — programs/libkarmdesk will
// wrap them in a typed gui.h once every app is on libkarm. Using the
// raw trampolines today means libkarmdesk's later wrappers will be a
// signature change, not a behaviour change.

#include <stdint.h>

#include "libkarm/syscall.h"
#include "libkarm/errno.h"
#include "kernel/syscall_numbers.h"

// programs/apps/image.ld now also picks up the default .text* and
// .rodata* sections, so this file's C functions and string literals
// land in the flat image without an explicit section attribute.

#define WIN_W            200
#define WIN_H             80
#define TITLE_BAR_H       12
#define EVENT_CAP          4
#define YIELDS_PER_SEC   200

// gui_event_t must match kernel/gui.h: type(u32) data1(i32) data2(i32).
typedef struct {
    uint32_t type;
    int32_t  data1;
    int32_t  data2;
} gui_event_t;

#define GUI_EVENT_KEY_PRESS  1U
#define GUI_EVENT_CLOSE      6U

// Window/compositor syscalls via the raw trampolines. The __syscallN
// helpers in libkarm/syscall.S take the syscall number in x0 and the
// arguments in x1..x6; libkarmdesk's gui.h will replace these with
// typed wrappers, leaving the call sites untouched below.

extern long __syscall1(long n, long a0);
extern long __syscall3(long n, long a0, long a1, long a2);
extern long __syscall6(long n, long a0, long a1, long a2,
                       long a3, long a4, long a5);

static long win_create(long x, long y, long w, long h,
                       long bg, long border) {
    // SYSCALLS.md documents sys_window_create with a title_ptr in x6,
    // but the kernel's syscall_dispatch only reads frame->x[0..5]; the
    // title is set through sys_window_set_title (75) right after.
    // libkarmdesk's future gui_window_create wrapper will inline-asm
    // x6 so the documented ABI is honoured; for now, drop the title
    // arg and rely on set_title below.
    return __syscall6(SYS_WINDOW_CREATE, x, y, w, h, bg, border);
}

static long win_destroy(long wid) {
    return __syscall1(SYS_WINDOW_DESTROY, wid);
}

static long win_set_title(long wid, const char *title, long h) {
    return __syscall3(SYS_WINDOW_SET_TITLE, wid, (long)(uintptr_t)title, h);
}

static long win_draw_rect(long wid, long x, long y, long w, long h,
                          long color) {
    return __syscall6(SYS_WINDOW_DRAW_RECT, wid, x, y, w, h, color);
}

static long win_draw_text(long wid, long x, long y, long color,
                          const char *str) {
    // SYS_WINDOW_DRAW_TEXT args: wid=x0, x=x1, y=x2, color=x3, str=x4.
    return __syscall6(SYS_WINDOW_DRAW_TEXT, wid, x, y, color,
                      (long)(uintptr_t)str, 0);
}

static long win_flush(long wid, long x, long y, long w, long h) {
    // SYS_WINDOW_FLUSH args: wid=x0, x=x1, y=x2, w=x3, h=x4.
    return __syscall6(SYS_WINDOW_FLUSH, wid, x, y, w, h, 0);
}

static long win_event(long wid, gui_event_t *buf, long cap) {
    return __syscall3(SYS_WINDOW_EVENT, wid, (long)(uintptr_t)buf, cap);
}

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
    long rc = kli_timeinfo(info);
    if (rc < 0) {
        return;
    }
    char text[9];
    format_hhmmss(info[0], text);

    // Clear the previous text rect (same coords the asm version used).
    (void)win_draw_rect(wid, 20, 30, WIN_W - 40, 28, 0xff202830ULL);

    // Draw the centered HH:MM:SS at (40, 34).
    (void)win_draw_text(wid, 40, 34, 0xffe0e8f0ULL, text);

    // Flush the content area (below the kernel-drawn title bar).
    (void)win_flush(wid, 0, 0, WIN_W, WIN_H - TITLE_BAR_H);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write_cstr(1, "clock: starting\n");

    long wid = win_create(440, 64, WIN_W, WIN_H,
                          0xff202830LL, 0xff808080LL);
    if (wid < 0) {
        write_cstr(1, "clock: window create failed\n");
        return 1;
    }
    (void)win_set_title(wid, "clock", TITLE_BAR_H);

    gui_event_t events[EVENT_CAP];

    for (;;) {
        // Yield ~200 times so other apps get a fair share of the CPU
        // and ~1 second passes between redraws.
        for (int i = 0; i < YIELDS_PER_SEC; i++) {
            (void)kli_yield();
        }

        redraw((long)wid);

        // Drain pending events; close on GUI_EVENT_CLOSE.
        long n = win_event((long)wid, events, EVENT_CAP);
        if (n > 0) {
            for (long i = 0; i < n; i++) {
                if (events[i].type == GUI_EVENT_CLOSE) {
                    (void)win_destroy((long)wid);
                    return 0;
                }
            }
        }
    }
}
