// programs/libkarmdesk/gui.h
//
// Typed wrappers for the KolibriARM window/compositor syscalls
// (numbers 70..80). This directory is deliberately separate from
// programs/libkarm because the GUI ABI is the one still actively
// growing — sys_window_get_bounds, sys_window_set_bounds, show/hide,
// and the resize event are all reserved but unimplemented per the
// current ROADMAP (items 1 and 4). When those land, they get a
// SYSCALLS.md row + syscall_numbers.h entry + dispatch case + host
// test in one commit, and only libkarmdesk needs a new wrapper —
// libkarm stays untouched.
//
// Functions:
// - gui_window_create    -> SYS_WINDOW_CREATE     (70)
// - gui_window_destroy   -> SYS_WINDOW_DESTROY    (71)
// - gui_window_draw_text -> SYS_WINDOW_DRAW_TEXT  (72)
// - gui_window_draw_rect -> SYS_WINDOW_DRAW_RECT  (73)
// - gui_window_event     -> SYS_WINDOW_EVENT      (74)
// - gui_window_set_title -> SYS_WINDOW_SET_TITLE  (75)
// - gui_window_redraw    -> SYS_WINDOW_REDRAW     (76)
// - gui_window_focus     -> SYS_WINDOW_FOCUS      (77)
// - gui_window_for_pid   -> SYS_WINDOW_FOR_PID    (78)
// - gui_cursor_set_shape -> SYS_CURSOR_SET_SHAPE  (79)
// - gui_window_flush     -> SYS_WINDOW_FLUSH      (80)
//
// Return value: raw `long` from the kernel; >= 0 on success,
// negative error code from <libkarm/errno.h> on failure.
//
// gui_event_t is the packed triple layout documented in SYSCALLS.md.
// Its 12-byte size is frozen — see the matching static_assert below.

#ifndef KOLIBRIARM_PROGRAMS_LIBKARMDESK_GUI_H
#define KOLIBRIARM_PROGRAMS_LIBKARMDESK_GUI_H

#include <stddef.h>
#include <stdint.h>

#include "libkarm/errno.h"
#include "libkarm/syscall.h"
#include "kernel/syscall_numbers.h"

typedef struct {
    uint32_t type;
    int32_t  data1;
    int32_t  data2;
} gui_event_t;

_Static_assert(sizeof(gui_event_t) == 12,
               "ABI drift: gui_event_t must be packed 12 bytes "
               "— see SYSCALLS.md GUI event layout");

// gui_event_t types, matching kernel/gui.h.
#define GUI_EVENT_KEY_PRESS   1U
#define GUI_EVENT_KEY_RELEASE 2U
#define GUI_EVENT_MOUSE_CLICK 3U
#define GUI_EVENT_MOUSE_MOVE  4U
#define GUI_EVENT_RESIZE       5U
#define GUI_EVENT_CLOSE        6U

// cursor shape ids for gui_cursor_set_shape.
#define GUI_CURSOR_ARROW 0U
#define GUI_CURSOR_HAND  1U

// Most window syscalls fit into libkarm's existing __syscallN
// trampolines. The single exception is sys_window_create: the
// documented ABI has 7 user arguments (x, y, w, h, bg, border,
// title_ptr) but libkarm's trampolines stop at 6. Inline asm here
// sets x6 = title_ptr and traps. The kernel's current
// syscall_dispatch only reads frame->x[0..5] — title_ptr is ignored
// today, and apps still call gui_window_set_title immediately after
// to set the title — but matching the documented ABI keeps the
// wrapper forward-compatible with the eventual fix.

static inline long gui_window_create(long x, long y, long w, long h,
                                     long bg, long border,
                                     const char *title) {
    register long x0 __asm__("x0") = x;
    register long x1 __asm__("x1") = y;
    register long x2 __asm__("x2") = w;
    register long x3 __asm__("x3") = h;
    register long x4 __asm__("x4") = bg;
    register long x5 __asm__("x5") = border;
    register long x6 __asm__("x6") = (long)(uintptr_t)title;
    register long x8 __asm__("x8") = SYS_WINDOW_CREATE;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3),
                       "r"(x4), "r"(x5), "r"(x6), "r"(x8)
                     : "memory", "cc");
    return x0;
}

static inline long gui_window_destroy(long window_id) {
    return __syscall1(SYS_WINDOW_DESTROY, window_id);
}

// SYS_WINDOW_DRAW_TEXT args: wid=x0, x=x1, y=x2, color=x3, str=x4.
static inline long gui_window_draw_text(long window_id, long x, long y,
                                       long color, const char *str) {
    return __syscall6(SYS_WINDOW_DRAW_TEXT, window_id, x, y, color,
                      (long)(uintptr_t)str, 0);
}

// SYS_WINDOW_DRAW_RECT args: wid=x0, x=x1, y=x2, w=x3, h=x4, color=x5.
static inline long gui_window_draw_rect(long window_id, long x, long y,
                                       long w, long h, long color) {
    return __syscall6(SYS_WINDOW_DRAW_RECT, window_id, x, y, w, h, color);
}

static inline long gui_window_event(long window_id, gui_event_t *buf,
                                   size_t max_events) {
    return __syscall3(SYS_WINDOW_EVENT, window_id,
                      (long)(uintptr_t)buf, (long)max_events);
}

static inline long gui_window_set_title(long window_id, const char *title,
                                        long title_h) {
    return __syscall3(SYS_WINDOW_SET_TITLE, window_id,
                      (long)(uintptr_t)title, title_h);
}

static inline long gui_window_redraw(long window_id) {
    return __syscall1(SYS_WINDOW_REDRAW, window_id);
}

static inline long gui_window_focus(long window_id) {
    return __syscall1(SYS_WINDOW_FOCUS, window_id);
}

static inline long gui_window_for_pid(long owner_pid, size_t index) {
    return __syscall2(SYS_WINDOW_FOR_PID, owner_pid, (long)index);
}

static inline long gui_cursor_set_shape(long shape) {
    return __syscall1(SYS_CURSOR_SET_SHAPE, shape);
}

// SYS_WINDOW_FLUSH args: wid=x0, x=x1, y=x2, w=x3, h=x4.
static inline long gui_window_flush(long window_id, long x, long y,
                                   long w, long h) {
    return __syscall6(SYS_WINDOW_FLUSH, window_id, x, y, w, h, 0);
}

// gui_window_get_bounds reads the window's (x, y, w, h) into out_ptr
// (4 x uint32_t = 16 bytes). out_ptr must point into a registered
// user region. Only the owning process may read another process's
// window bounds.
static inline long gui_window_get_bounds(long window_id, void *out_ptr) {
    return __syscall2(SYS_WINDOW_GET_BOUNDS, window_id,
                      (long)(uintptr_t)out_ptr);
}

// gui_window_set_bounds moves and/or resizes the window in one step.
// If the new (w, h) differs from the current size the kernel
// reallocates the per-window backing and pushes GUI_EVENT_RESIZE
// onto the owner's event queue so the app can rebuild its layout.
static inline long gui_window_set_bounds(long window_id, long x, long y,
                                         long w, long h) {
    return __syscall6(SYS_WINDOW_SET_BOUNDS, window_id, x, y, w, h, 0);
}

#endif
