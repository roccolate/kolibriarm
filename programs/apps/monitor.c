// KolibriARM app: monitor (C version, on libkarm + libkarmdesk)
//
// Windowed process monitor. It owns a desktop window, redraws memory
// and tick counters plus a short process list once every REDRAW_WAIT
// yields, and exits on 'q', 'Q', or title-bar close.
//
// Non-window syscalls (write, yield, meminfo, timeinfo, proclist,
// exit) go through libkarm's typed wrappers. Number formatting uses
// kli_utoa from <libkarm/string.h>. Window syscalls go through
// libkarmdesk's typed gui_* wrappers.

#include <stddef.h>
#include <stdint.h>

#include "libkarm/syscall.h"
#include "libkarm/string.h"
#include "libkarm/errno.h"
#include "libkarmdesk/gui.h"

#define WIN_X          56
#define WIN_Y         120
#define WIN_W         340
#define WIN_H         220
#define TITLE_BAR_H    16
#define EVENT_CAP       8
#define PROC_CAP        6
#define REDRAW_WAIT    20

#define COLOR_BG       0xff182024U
#define COLOR_BORDER   0xff809080U
#define COLOR_TEXT     0xffd0e0d0U

static void write_cstr(long fd, const char *s) {
    while (*s) {
        (void)kli_write((int)fd, s, 1);
        s++;
    }
}

static void draw_text(long wid, long x, long y, const char *s) {
    (void)gui_window_draw_text(wid, x, y, COLOR_TEXT, s);
}

// sys_proclist entry: pid(u32) state(u32) name[16] -> 24 bytes.
typedef struct {
    uint32_t pid;
    uint32_t state;
    char     name[16];
} proc_entry_t;

static void redraw(long wid, uint64_t *info, char *numbuf,
                   proc_entry_t *procs) {
    (void)gui_window_draw_rect(wid, 1, 0, WIN_W - 2,
                               WIN_H - TITLE_BAR_H - 2, COLOR_BG);

    draw_text(wid, 12, 8, "SYSTEM MONITOR");

    if (kli_meminfo(info) >= 0) {
        draw_text(wid, 12, 28, "FREE PG");
        kli_utoa(info[1], numbuf, 24);
        draw_text(wid, 96, 28, numbuf);
    }

    if (kli_timeinfo(info) >= 0) {
        draw_text(wid, 12, 44, "TICKS");
        kli_utoa(info[0], numbuf, 24);
        draw_text(wid, 96, 44, numbuf);
    }

    draw_text(wid, 12, 68, "PID   ST    NAME");

    long n = kli_proclist(procs, PROC_CAP);
    if (n < 0) {
        n = 0;
    }
    long y = 84;
    for (long i = 0; i < n && i < PROC_CAP; i++) {
        kli_utoa((uint64_t)procs[i].pid, numbuf, 24);
        draw_text(wid, 12, y, numbuf);
        kli_utoa((uint64_t)procs[i].state, numbuf, 24);
        draw_text(wid, 56, y, numbuf);
        // name is a 16-byte fixed field; draw it as a cstring by
        // forcing a NUL terminator if the kernel didn't.
        char name_buf[17];
        for (int j = 0; j < 16; j++) {
            char c = procs[i].name[j];
            name_buf[j] = (c == '\0') ? '\0' : c;
        }
        name_buf[16] = '\0';
        draw_text(wid, 108, y, name_buf);
        y += 16;
    }

    (void)gui_window_flush(wid, 0, 0, WIN_W, WIN_H - TITLE_BAR_H);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write_cstr(1, "monitor: starting\n");

    long wid = gui_window_create(WIN_X, WIN_Y, WIN_W, WIN_H,
                                 COLOR_BG, COLOR_BORDER, "monitor");
    if (wid < 0) {
        write_cstr(1, "monitor: window create failed\n");
        return 1;
    }
    (void)gui_window_set_title(wid, "monitor", TITLE_BAR_H);

    uint64_t info[3];
    char     numbuf[24];
    proc_entry_t procs[PROC_CAP];
    gui_event_t events[EVENT_CAP];

    long wait = REDRAW_WAIT;
    redraw(wid, info, numbuf, procs);

    for (;;) {
        long n = gui_window_event(wid, events, EVENT_CAP);
        if (n > 0) {
            for (long i = 0; i < n; i++) {
                if (events[i].type == GUI_EVENT_CLOSE) {
                    (void)gui_window_destroy(wid);
                    return 0;
                }
                if (events[i].type == GUI_EVENT_KEY_PRESS &&
                    (events[i].data1 == 'q' || events[i].data1 == 'Q')) {
                    (void)gui_window_destroy(wid);
                    return 0;
                }
            }
        }

        wait--;
        if (wait <= 0) {
            redraw(wid, info, numbuf, procs);
            wait = REDRAW_WAIT;
        }

        (void)kli_yield();
    }
}
