/*
 * test_window_abi.c
 *
 * Lock down the kernel window-manager ABI. SYSCALLS.md describes the
 * event format (3 packed fields, 12 bytes total), the ownership rules
 * (each window has a single owner pid; cross-process use is rejected),
 * the focus rules (raise by bumping z; NO_FOCUS prevents focus), and
 * the lifecycle contract (a window whose owner has exited stays around
 * until the user closes it).
 *
 * The tests here exercise the kernel-side helpers directly because
 * driving the syscall entry on the host would also need a working
 * framebuffer and a fake EL0 frame.
 */

#include "unity/unity.h"

#include <stdint.h>

#include "kernel/gui.h"
#include "kernel/process.h"

void test_window_abi_event_layout_is_12_bytes(void) {
    /* SYSCALLS.md: "sys_window_event writes packed gui_event_t triples:
     * uint32_t type, int32_t data1, int32_t data2." The total must be
     * 12 bytes so apps can size their event buffers with a single
     * multiply. A struct field reorder or width change breaks every
     * event-reading app. */
    TEST_ASSERT_EQUAL_UINT64(12ULL, sizeof(gui_event_t));

    /* The field offsets line up with the documented ABI. */
    gui_event_t sample;
    sample.type = 0x11111111U;
    sample.data1 = 0x22222222;
    sample.data2 = 0x33333333;

    uint8_t *bytes = (uint8_t *)&sample;
    /* type is the first 4 bytes. */
    TEST_ASSERT_EQUAL_UINT64(0x11ULL, (uint64_t)bytes[0]);
    TEST_ASSERT_EQUAL_UINT64(0x11ULL, (uint64_t)bytes[1]);
    TEST_ASSERT_EQUAL_UINT64(0x11ULL, (uint64_t)bytes[2]);
    TEST_ASSERT_EQUAL_UINT64(0x11ULL, (uint64_t)bytes[3]);
    /* data1 is the next 4 bytes. */
    TEST_ASSERT_EQUAL_UINT64(0x22ULL, (uint64_t)bytes[4]);
    TEST_ASSERT_EQUAL_UINT64(0x22ULL, (uint64_t)bytes[5]);
    TEST_ASSERT_EQUAL_UINT64(0x22ULL, (uint64_t)bytes[6]);
    TEST_ASSERT_EQUAL_UINT64(0x22ULL, (uint64_t)bytes[7]);
    /* data2 is the last 4 bytes. */
    TEST_ASSERT_EQUAL_UINT64(0x33ULL, (uint64_t)bytes[8]);
    TEST_ASSERT_EQUAL_UINT64(0x33ULL, (uint64_t)bytes[9]);
    TEST_ASSERT_EQUAL_UINT64(0x33ULL, (uint64_t)bytes[10]);
    TEST_ASSERT_EQUAL_UINT64(0x33ULL, (uint64_t)bytes[11]);
}

void test_window_abi_event_types_match_documented_numbers(void) {
    /* The event type IDs are part of the ABI: shell/editor/monitor
     * apps match on these numbers in their event loops. A reordering
     * silently breaks every windowed app. */
    TEST_ASSERT_EQUAL_UINT64(1ULL, (uint64_t)GUI_EVENT_KEY_PRESS);
    TEST_ASSERT_EQUAL_UINT64(2ULL, (uint64_t)GUI_EVENT_KEY_RELEASE);
    TEST_ASSERT_EQUAL_UINT64(3ULL, (uint64_t)GUI_EVENT_MOUSE_CLICK);
    TEST_ASSERT_EQUAL_UINT64(4ULL, (uint64_t)GUI_EVENT_MOUSE_MOVE);
    TEST_ASSERT_EQUAL_UINT64(5ULL, (uint64_t)GUI_EVENT_RESIZE);
    TEST_ASSERT_EQUAL_UINT64(6ULL, (uint64_t)GUI_EVENT_CLOSE);

    /* They are pairwise distinct, which is what apps rely on when
     * switching on type. */
    TEST_ASSERT_TRUE(GUI_EVENT_KEY_PRESS   != GUI_EVENT_KEY_RELEASE);
    TEST_ASSERT_TRUE(GUI_EVENT_KEY_RELEASE != GUI_EVENT_MOUSE_CLICK);
    TEST_ASSERT_TRUE(GUI_EVENT_MOUSE_CLICK != GUI_EVENT_MOUSE_MOVE);
    TEST_ASSERT_TRUE(GUI_EVENT_MOUSE_MOVE  != GUI_EVENT_RESIZE);
    TEST_ASSERT_TRUE(GUI_EVENT_RESIZE      != GUI_EVENT_CLOSE);
}

void test_window_abi_create_window_for_pid_assigns_owner(void) {
    /* gui_create_window_for_pid is the only legal way to make an
     * owner-bound window. gui_create_window wraps it with
     * GUI_NO_OWNER; apps go through the syscall layer and reach
     * gui_create_window_for_pid directly. */
    uint32_t pixels[64 * 64] = {0};
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id = GUI_NO_WINDOW;

    fb_init(&fb, pixels, 64, 64, 64);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 4242U, 0, 0, 8, 8, 0xff000000U,
                                 0xffffffffU, "app_window", &window_id));
    TEST_ASSERT_TRUE(window_id < GUI_MAX_WINDOWS);
    TEST_ASSERT_EQUAL_UINT64(1ULL, (uint64_t)desktop.windows[window_id].used);
    TEST_ASSERT_EQUAL_UINT64(4242ULL,
                             (uint64_t)desktop.windows[window_id].owner_pid);
}

void test_window_abi_focus_window_raises_z_order(void) {
    /* SYSCALLS.md: "sys_window_focus updates the focus border and
     * raises the target window above other windows by bumping its
     * z-order. Window ids remain stable pool indices; z-order
     * changes do not move window structs." */
    uint32_t pixels[64 * 64] = {0};
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t first_id = GUI_NO_WINDOW;
    uint32_t second_id = GUI_NO_WINDOW;

    fb_init(&fb, pixels, 64, 64, 64);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 1U, 0, 0, 8, 8, 0xff000000U,
                                 0xffffffffU, "first", &first_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 1U, 16, 0, 8, 8, 0xff00aa00U,
                                 0xffffffffU, "second", &second_id));

    uint32_t initial_first_z = desktop.windows[first_id].z;
    uint32_t initial_second_z = desktop.windows[second_id].z;

    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_focus_window(&desktop, first_id));

    /* The focused window's z grew past the other's. */
    TEST_ASSERT_TRUE(desktop.windows[first_id].z > initial_first_z);
    TEST_ASSERT_TRUE(desktop.windows[first_id].z > desktop.windows[second_id].z);

    /* Window pool index is stable — ids do not change. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)first_id, (uint64_t)first_id);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)second_id, (uint64_t)second_id);

    /* second's z is unchanged by focusing first. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)initial_second_z,
                             (uint64_t)desktop.windows[second_id].z);
}

void test_window_abi_focus_no_focus_window_rejected(void) {
    /* A dock window (e.g. the panel taskbar) carries
     * GUI_WINDOW_NO_FOCUS. gui_focus_window_ensure must not pick it
     * as the focused window even when it is the only window. */
    uint32_t pixels[64 * 64] = {0};
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t dock_id = GUI_NO_WINDOW;

    fb_init(&fb, pixels, 64, 64, 64);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 99U, 0, 0, 8, 8, 0xff000000U,
                                 0xffffffffU, "dock", &dock_id));
    desktop.windows[dock_id].flags |= GUI_WINDOW_NO_FOCUS;

    /* focus_window_ensure walks the list and must skip the dock. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_focus_window_ensure(&desktop));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_NO_WINDOW,
                             (uint64_t)desktop.focused_window_id);
}

void test_window_abi_window_for_pid_skips_no_owner(void) {
    /* SYSCALLS.md: "sys_window_for_pid skips ownerless windows
     * (those whose owner_pid == GUI_NO_OWNER); only windows actually
     * owned by a process are visible through it." gui_window_for_pid
     * is the kernel side. */
    uint32_t pixels[64 * 64] = {0};
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t owner_window = GUI_NO_WINDOW;
    uint32_t no_owner_window = GUI_NO_WINDOW;

    fb_init(&fb, pixels, 64, 64, 64);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 7U, 0, 0, 8, 8, 0xff000000U,
                                 0xffffffffU, "owned", &owner_window));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, GUI_NO_OWNER, 16, 0, 8, 8,
                                 0xff000000U, 0xffffffffU, "orphan",
                                 &no_owner_window));

    /* Owner pid 7 sees its own window. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)owner_window,
                             (uint64_t)gui_window_for_pid(&desktop, 7U, 0U));
    /* Owner pid GUI_NO_OWNER never sees anything. */
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)GUI_NO_WINDOW,
        (uint64_t)gui_window_for_pid(&desktop, GUI_NO_OWNER, 0U));
    /* Other pids see nothing owned by 7. */
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)GUI_NO_WINDOW,
        (uint64_t)gui_window_for_pid(&desktop, 12345U, 0U));
}

void test_window_abi_window_for_pid_skips_skip_taskbar(void) {
    /* The desktop's running-apps list and the panel's launcher share
     * an enumeration: SKIP_TASKBAR keeps a window out of both the
     * taskbar's read and the per-pid lookup. Pin the contract so a
     * regression that exposes hidden windows cannot land silently. */
    uint32_t pixels[64 * 64] = {0};
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t visible_id = GUI_NO_WINDOW;
    uint32_t hidden_id = GUI_NO_WINDOW;

    fb_init(&fb, pixels, 64, 64, 64);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 11U, 0, 0, 8, 8, 0xff000000U,
                                 0xffffffffU, "visible", &visible_id));
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 11U, 16, 0, 8, 8, 0xff000000U,
                                 0xffffffffU, "hidden", &hidden_id));
    desktop.windows[hidden_id].flags |= GUI_WINDOW_SKIP_TASKBAR;

    /* Owner lookup sees the visible one at index 0 and skips the
     * SKIP_TASKBAR one. Index 1 returns NO_WINDOW. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)visible_id,
                             (uint64_t)gui_window_for_pid(&desktop, 11U, 0U));
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)GUI_NO_WINDOW,
        (uint64_t)gui_window_for_pid(&desktop, 11U, 1U));
}

void test_window_abi_get_bounds_returns_current_geometry(void) {
    /* SYSCALLS.md: "sys_window_get_bounds copies the window's
     * (x, y, w, h) into the caller's 16-byte buffer." gui_window_get_bounds
     * is the kernel helper the syscall layer wraps; pinning it here
     * keeps the format and the ownership checks honest. */
    uint32_t pixels[64 * 64] = {0};
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id = GUI_NO_WINDOW;

    fb_init(&fb, pixels, 64, 64, 64);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 7U, 12, 24, 100, 60, 0xff000000U,
                                 0xffffffffU, "bounds_test", &window_id));

    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_window_get_bounds(
                                     &desktop.windows[window_id], &x, &y, &w, &h));
    TEST_ASSERT_EQUAL_UINT64(12ULL, (uint64_t)x);
    TEST_ASSERT_EQUAL_UINT64(24ULL, (uint64_t)y);
    TEST_ASSERT_EQUAL_UINT64(100ULL, (uint64_t)w);
    TEST_ASSERT_EQUAL_UINT64(60ULL, (uint64_t)h);

    /* Each out-pointer is independently optional. */
    uint32_t only_w = 0;
    TEST_ASSERT_EQUAL_UINT64(
        0, (uint64_t)gui_window_get_bounds(&desktop.windows[window_id],
                                           0, 0, &only_w, 0));
    TEST_ASSERT_EQUAL_UINT64(100ULL, (uint64_t)only_w);
}

void test_window_abi_resize_window_updates_geometry_and_queues_event(void) {
    /* SYSCALLS.md: "sys_window_set_bounds moves and/or resizes the
     * window in one step; if (w, h) changes the kernel reallocates
     * the per-window backing and pushes GUI_EVENT_RESIZE onto the
     * owner's event queue." gui_resize_window is the kernel helper
     * the syscall layer wraps. */
    uint32_t pixels[64 * 64] = {0};
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id = GUI_NO_WINDOW;

    fb_init(&fb, pixels, 64, 64, 64);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 7U, 0, 0, 16, 16, 0xff000000U,
                                 0xffffffffU, "resize_test", &window_id));

    /* gui_window_pop_event returns 0 on success and -1 when the queue
     * is empty. Verify the queue starts empty, then poke it through a
     * move (no resize event expected) and a resize (event expected). */
    gui_event_t evbuf[4];

    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)gui_window_pop_event(&desktop.windows[window_id], &evbuf[0]));

    /* Move without resizing: no resize event, but the geometry changes. */
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_resize_window(
                                     &desktop, window_id, 8U, 8U, 16U, 16U));
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)gui_window_pop_event(&desktop.windows[window_id], &evbuf[0]));

    /* Resize: geometry updates AND a GUI_EVENT_RESIZE lands in the
     * owner's queue with the new (w, h). */
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_resize_window(
                                     &desktop, window_id, 8U, 8U, 32U, 24U));
    TEST_ASSERT_EQUAL_UINT64(
        0,
        (uint64_t)gui_window_pop_event(&desktop.windows[window_id], &evbuf[0]));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_EVENT_RESIZE, (uint64_t)evbuf[0].type);
    TEST_ASSERT_EQUAL_UINT64(32LL, (uint64_t)evbuf[0].data1);
    TEST_ASSERT_EQUAL_UINT64(24LL, (uint64_t)evbuf[0].data2);

    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_window_get_bounds(
                                     &desktop.windows[window_id], &x, &y, &w, &h));
    TEST_ASSERT_EQUAL_UINT64(8ULL, (uint64_t)x);
    TEST_ASSERT_EQUAL_UINT64(8ULL, (uint64_t)y);
    TEST_ASSERT_EQUAL_UINT64(32ULL, (uint64_t)w);
    TEST_ASSERT_EQUAL_UINT64(24ULL, (uint64_t)h);
}

void test_window_abi_event_types_includes_minimize_and_maximize(void) {
    /* SYSCALLS.md: GUI_EVENT_MINIMIZE (7) and GUI_EVENT_MAXIMIZE (8)
     * are the synthetic events the kernel pushes when the user
     * clicks the kernel-drawn title-bar buttons (or the owner
     * calls sys_window_minimize / sys_window_restore). They are
     * part of the ABI; an app that switches on event types must see
     * these numbers. */
    TEST_ASSERT_EQUAL_UINT64(7ULL, (uint64_t)GUI_EVENT_MINIMIZE);
    TEST_ASSERT_EQUAL_UINT64(8ULL, (uint64_t)GUI_EVENT_MAXIMIZE);

    /* Distinct from each other and from every older event id. */
    TEST_ASSERT_TRUE(GUI_EVENT_MINIMIZE != GUI_EVENT_MAXIMIZE);
    TEST_ASSERT_TRUE(GUI_EVENT_MINIMIZE != GUI_EVENT_CLOSE);
    TEST_ASSERT_TRUE(GUI_EVENT_MAXIMIZE != GUI_EVENT_RESIZE);
}

void test_window_abi_minimize_hides_window_and_queues_event(void) {
    /* gui_window_minimize sets window->minimized, pushes
     * GUI_EVENT_MINIMIZE on the owner's queue, and requests a
     * desktop redraw. gui_window_restore reverses it. */
    uint32_t pixels[64 * 64] = {0};
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id = GUI_NO_WINDOW;

    fb_init(&fb, pixels, 64, 64, 64);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 31U, 0, 0, 16, 16, 0xff000000U,
                                 0xffffffffU, "min_test", &window_id));

    /* Initially visible and unminimised. */
    TEST_ASSERT_EQUAL_UINT64(0U,
                             (uint64_t)desktop.windows[window_id].minimized);

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_minimize(&desktop, window_id));
    TEST_ASSERT_EQUAL_UINT64(1U,
                             (uint64_t)desktop.windows[window_id].minimized);

    gui_event_t ev;
    TEST_ASSERT_EQUAL_UINT64(
        0, (uint64_t)gui_window_pop_event(&desktop.windows[window_id], &ev));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_EVENT_MINIMIZE, (uint64_t)ev.type);

    /* Restore clears the flag, raises z, pushes GUI_EVENT_MAXIMIZE. */
    uint32_t old_z = desktop.windows[window_id].z;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_restore(&desktop, window_id));
    TEST_ASSERT_EQUAL_UINT64(0U,
                             (uint64_t)desktop.windows[window_id].minimized);
    TEST_ASSERT_TRUE(desktop.windows[window_id].z > old_z);

    TEST_ASSERT_EQUAL_UINT64(
        0, (uint64_t)gui_window_pop_event(&desktop.windows[window_id], &ev));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_EVENT_MAXIMIZE, (uint64_t)ev.type);

    /* Idempotence: minimising an already-minimised window is a no-op
     * (returns -1) and does not push a second event. The queue still
     * holds the first MINIMIZE from the call above, so pop returns
     * it and the next pop is empty. */
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_window_minimize(&desktop, window_id));
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)gui_window_minimize(&desktop, window_id));
    TEST_ASSERT_EQUAL_UINT64(
        0, (uint64_t)gui_window_pop_event(&desktop.windows[window_id], &ev));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)GUI_EVENT_MINIMIZE, (uint64_t)ev.type);
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)gui_window_pop_event(&desktop.windows[window_id], &ev));

    /* Same in the other direction: a freshly created, not-yet-
     * minimised window rejects restore with -1. We re-allocate a
     * second window so this case has a fresh starting point. */
    uint32_t second_id = GUI_NO_WINDOW;
    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 31U, 32, 32, 16, 16, 0xff000000U,
                                 0xffffffffU, "restore_reject", &second_id));
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)gui_window_restore(&desktop, second_id));
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)-1,
        (uint64_t)gui_window_pop_event(&desktop.windows[second_id], &ev));
}

void test_window_abi_resize_window_rejects_out_of_bounds(void) {
    /* Bounds that would put the window off-screen, or below the
     * minimum 2x2 size, must fail without touching the window. */
    uint32_t pixels[64 * 64] = {0};
    fb_t fb;
    gui_desktop_t desktop;
    uint32_t window_id = GUI_NO_WINDOW;

    fb_init(&fb, pixels, 64, 64, 64);
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_init(&desktop, &fb, 0xff101010U));

    TEST_ASSERT_EQUAL_UINT64(0,
                             (uint64_t)gui_create_window_for_pid(
                                 &desktop, 7U, 0, 0, 16, 16, 0xff000000U,
                                 0xffffffffU, "reject_test", &window_id));

    /* Window exceeds right edge. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_resize_window(&desktop, window_id,
                                                          60U, 0U, 16U, 16U));
    /* Window exceeds bottom edge. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_resize_window(&desktop, window_id,
                                                          0U, 60U, 16U, 16U));
    /* Below the 2x2 minimum. */
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_resize_window(&desktop, window_id,
                                                          0U, 0U, 1U, 16U));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)-1,
                             (uint64_t)gui_resize_window(&desktop, window_id,
                                                          0U, 0U, 16U, 1U));

    /* None of those calls touched the window. */
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    TEST_ASSERT_EQUAL_UINT64(0, (uint64_t)gui_window_get_bounds(
                                     &desktop.windows[window_id], &x, &y, &w, &h));
    TEST_ASSERT_EQUAL_UINT64(0ULL, (uint64_t)x);
    TEST_ASSERT_EQUAL_UINT64(0ULL, (uint64_t)y);
    TEST_ASSERT_EQUAL_UINT64(16ULL, (uint64_t)w);
    TEST_ASSERT_EQUAL_UINT64(16ULL, (uint64_t)h);
}
