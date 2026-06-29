// programs/libkarmdesk/gui.h
//
// Typed wrappers for the ArmoniOS window/compositor syscalls
// (numbers 70..86). This directory is deliberately separate from
// programs/libkarm so desktop-facing wrappers can grow without
// touching the stable process / memory / I/O / IPC surface.
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
// - gui_window_get_bounds -> SYS_WINDOW_GET_BOUNDS (81)
// - gui_window_set_bounds -> SYS_WINDOW_SET_BOUNDS (82)
// - gui_window_minimize  -> SYS_WINDOW_MINIMIZE   (83)
// - gui_window_restore   -> SYS_WINDOW_RESTORE    (84)
// - gui_window_state     -> SYS_WINDOW_STATE      (85)
// - gui_cursor_register_region -> SYS_CURSOR_REGISTER_REGION (86)
//
// Return value: raw `long` from the kernel; >= 0 on success,
// negative error code from <libkarm/errno.h> on failure.
//
// gui_event_t is the packed triple layout documented in docs/SYSCALLS.md.
// Its 12-byte size is frozen — see the matching static_assert below.

#ifndef ARMONIOS_PROGRAMS_LIBKARMDESK_GUI_H
#define ARMONIOS_PROGRAMS_LIBKARMDESK_GUI_H

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
               "— see docs/SYSCALLS.md GUI event layout");

// gui_event_t types, matching kernel/gui.h.
#define GUI_EVENT_KEY_PRESS   1U
#define GUI_EVENT_KEY_RELEASE 2U
#define GUI_EVENT_MOUSE_CLICK 3U
#define GUI_EVENT_MOUSE_MOVE  4U
#define GUI_EVENT_RESIZE       5U
#define GUI_EVENT_CLOSE        6U
#define GUI_EVENT_MINIMIZE     7U
#define GUI_EVENT_MAXIMIZE     8U

// Window state bits returned by gui_window_state. Bit assignments
// match GUI_WINDOW_STATE_* in kernel/gui.h.
#define GUI_WINDOW_STATE_MINIMIZED 0x1U
#define GUI_WINDOW_STATE_FOCUSED   0x2U

// cursor shape ids for gui_cursor_set_shape.
#define GUI_CURSOR_ARROW 0U
#define GUI_CURSOR_HAND  1U

static inline long gui_window_create(long x, long y, long w, long h,
                                     long bg, long border,
                                     const char *title) {
    return __syscall7(SYS_WINDOW_CREATE, x, y, w, h, bg, border,
                      (long)(uintptr_t)title);
}

static inline long gui_window_destroy(long window_id) {
    return __syscall1(SYS_WINDOW_DESTROY, window_id);
}

// SYS_WINDOW_DRAW_TEXT args: wid=x0, x=x1, y=x2, color=x3, str=x4.
static inline long gui_window_draw_text(long window_id, long x, long y,
                                       long color, const char *str) {
    return __syscall5(SYS_WINDOW_DRAW_TEXT, window_id, x, y, color,
                      (long)(uintptr_t)str);
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
    return __syscall5(SYS_WINDOW_FLUSH, window_id, x, y, w, h);
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
    return __syscall5(SYS_WINDOW_SET_BOUNDS, window_id, x, y, w, h);
}

// gui_window_minimize: owner-only; hides the window through the same
// path the kernel-drawn minimize button uses. The kernel pushes
// GUI_EVENT_MINIMIZE on the owner's event queue.
static inline long gui_window_minimize(long window_id) {
    return __syscall1(SYS_WINDOW_MINIMIZE, window_id);
}

// gui_window_restore: inverse of gui_window_minimize. This is intentionally
// callable by the panel for another process's minimized window, just like
// gui_window_focus. It clears the hidden flag, raises the window, and pushes
// GUI_EVENT_MAXIMIZE so apps that resize on maximise can rebuild.
static inline long gui_window_restore(long window_id) {
    return __syscall1(SYS_WINDOW_RESTORE, window_id);
}

// gui_window_state: writes a 32-bit state bitmap into out_ptr. Read-only
// presentation state is visible to the panel so it can draw taskbar slots.
// Bit 0 = GUI_WINDOW_STATE_MINIMIZED, bit 1 = GUI_WINDOW_STATE_FOCUSED.
static inline long gui_window_state(long window_id, uint32_t *out_ptr) {
    return __syscall2(SYS_WINDOW_STATE, window_id, (long)(uintptr_t)out_ptr);
}

// gui_cursor_register_region: owner-only; install or replace a
// per-window cursor-shape region. slot is 0..7; x, y, w, h are
// content-local. Pass shape == 0xffffffff to clear the slot. The
// kernel walks the slots in ascending order during cursor refresh
// and uses the first region whose rect contains the cursor, so
// later slots in the list have lower priority.
static inline long gui_cursor_register_region(long window_id, long slot,
                                              long x, long y, long w,
                                              long h, long shape) {
    return __syscall7(SYS_CURSOR_REGISTER_REGION, window_id, slot, x, y,
                      w, h, shape);
}

#endif
